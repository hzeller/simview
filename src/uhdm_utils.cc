#include "uhdm_utils.h"
#include <uhdm/assignment.h>
#include <uhdm/begin.h>
#include <uhdm/bit_select.h>
#include <uhdm/cont_assign.h>
#include <uhdm/do_while.h>
#include <uhdm/event_control.h>
#include <uhdm/expr.h>
#include <uhdm/for_stmt.h>
#include <uhdm/gen_scope.h>
#include <uhdm/gen_scope_array.h>
#include <uhdm/if_else.h>
#include <uhdm/if_stmt.h>
#include <uhdm/module.h>
#include <uhdm/named_begin.h>
#include <uhdm/operation.h>
#include <uhdm/part_select.h>
#include <uhdm/port.h>
#include <uhdm/process_stmt.h>
#include <uhdm/ref_obj.h>
#include <uhdm/sv_vpi_user.h>
#include <uhdm/task_call.h>
#include <uhdm/tf_call.h>
#include <uhdm/uhdm_vpi_user.h>
#include <uhdm/vpi_user.h>
#include <uhdm/while_stmt.h>

namespace sv {
namespace {

// Forward declare the recurse call
void RecurseFindItem(const UHDM::any *haystack, const UHDM::any *needle,
                     bool drivers, std::vector<const UHDM::any *> *list);

// Both Modules and GenScopes can work as the Haystack here, they have the same
// set of methods. However, they are not virtual, so it's not possible to use
// the same code on some super type for haystack. A templated function will have
// to suffice instead.
template <typename T>
void FindInContainer(T *haystack, const UHDM::any *needle, bool drivers,
                     std::vector<const UHDM::any *> *list) {
  // Look through all continuous assignments.
  if (haystack->Cont_assigns() != nullptr) {
    for (auto &ca : *haystack->Cont_assigns()) {
      // See if the net is part of the LHS.
      RecurseFindItem(drivers ? ca->Lhs() : ca->Rhs(), needle, drivers, list);
    }
  }
  // Look through all process statements.
  if (haystack->Process() != nullptr) {
    for (auto &p : *haystack->Process()) {
      RecurseFindItem(p->Stmt(), needle, drivers, list);
    }
  }
  // Look through all instances, to see if the net is connected to a port.
  if (haystack->Modules() != nullptr) {
    for (auto sub : *haystack->Modules()) {
      if (sub->Ports() == nullptr) continue;
      for (auto p : *sub->Ports()) {
        // Skip ports output ports for loads, and input ports for drivers.
        if ((p->VpiDirection() == vpiOutput && !drivers) ||
            (p->VpiDirection() == vpiInput && drivers)) {
          continue;
        }
        RecurseFindItem(p->High_conn(), needle, drivers, list);
      }
    }
  }
}

void RecurseFindItem(const UHDM::any *haystack, const UHDM::any *needle,
                     bool drivers, std::vector<const UHDM::any *> *list) {
  if (haystack == nullptr) return;
  auto type = haystack->VpiType();
  if (type == vpiModule) {
    auto m = dynamic_cast<const UHDM::module *>(haystack);
    FindInContainer(m, needle, drivers, list);
  } else if (type == vpiGenScopeArray) {
    auto ga = dynamic_cast<const UHDM::gen_scope_array *>(haystack);
    auto g = (*ga->Gen_scopes())[0];
    FindInContainer(g, needle, drivers, list);
  } else if (type == vpiOperation) {
    auto op = dynamic_cast<const UHDM::operation *>(haystack);
    if (op->Operands() != nullptr) {
      // Recurse through the full expression.
      for (auto o : *op->Operands()) {
        RecurseFindItem(o, needle, drivers, list);
      }
    }
  } else if (type == vpiBegin) {
    auto b = dynamic_cast<const UHDM::begin *>(haystack);
    if (b->Stmts() != nullptr) {
      for (auto s : *b->Stmts()) {
        RecurseFindItem(s, needle, drivers, list);
      }
    }
  } else if (type == vpiNamedBegin) {
    auto nb = dynamic_cast<const UHDM::named_begin *>(haystack);
    if (nb->Stmts() != nullptr) {
      for (auto s : *nb->Stmts()) {
        RecurseFindItem(s, needle, drivers, list);
      }
    }
  } else if (type == vpiFuncCall || type == vpiTaskCall) {
    auto tfc = dynamic_cast<const UHDM::tf_call *>(haystack);
    if (tfc->Tf_call_args() != nullptr) {
      for (auto a : *tfc->Tf_call_args()) {
        RecurseFindItem(a, needle, drivers, list);
      }
    }
  } else if (type == vpiAssignment) {
    auto assignment = dynamic_cast<const UHDM::assignment *>(haystack);
    auto expr = drivers ? assignment->Lhs() : assignment->Rhs();
    RecurseFindItem(expr, needle, drivers, list);
  } else if (type == vpiEventControl) {
    auto ec = dynamic_cast<const UHDM::event_control *>(haystack);
    if (!drivers) RecurseFindItem(ec->VpiCondition(), needle, drivers, list);
    RecurseFindItem(ec->Stmt(), needle, drivers, list);
  } else if (type == vpiIf) {
    auto is = dynamic_cast<const UHDM::if_stmt *>(haystack);
    if (!drivers) RecurseFindItem(is->VpiCondition(), needle, drivers, list);
    RecurseFindItem(is->VpiStmt(), needle, drivers, list);
  } else if (type == vpiIfElse) {
    auto ie = dynamic_cast<const UHDM::if_else *>(haystack);
    if (!drivers) RecurseFindItem(ie->VpiCondition(), needle, drivers, list);
    RecurseFindItem(ie->VpiStmt(), needle, drivers, list);
    RecurseFindItem(ie->VpiElseStmt(), needle, drivers, list);
  } else if (type == vpiFor) {
    auto f = dynamic_cast<const UHDM::for_stmt *>(haystack);
    RecurseFindItem(f->VpiStmt(), needle, drivers, list);
  } else if (type == vpiWhile) {
    auto w = dynamic_cast<const UHDM::while_stmt *>(haystack);
    RecurseFindItem(w->VpiStmt(), needle, drivers, list);
  } else if (type == vpiDoWhile) {
    auto dw = dynamic_cast<const UHDM::do_while *>(haystack);
    RecurseFindItem(dw->VpiStmt(), needle, drivers, list);
  } else if (type == vpiBitSelect) {
    auto bs = dynamic_cast<const UHDM::bit_select *>(haystack);
    if (bs->VpiName() == needle->VpiName()) {
      list->push_back(haystack);
    }
    // if (bs->VpiParent() != nullptr && bs->VpiParent()->VpiType() ==
    // vpiRefObj) {
    //  auto ro = dynamic_cast<const UHDM::ref_obj *>(bs->VpiParent());
    //  if (ro->Actual_group() == needle) {
    //    list.push_back(haystack);
    //  }
    //} else if (bs->VpiParent() == needle) {
    //  list.push_back(haystack);
    //}
  } else if (type == vpiPartSelect) {
    auto ps = dynamic_cast<const UHDM::part_select *>(haystack);
    if (ps->VpiParent() != nullptr && ps->VpiParent()->VpiType() == vpiRefObj) {
      auto ro = dynamic_cast<const UHDM::ref_obj *>(ps->VpiParent());
      if (ro->Actual_group() == needle) {
        list->push_back(haystack);
      }
    }
  } else if (type == vpiRefObj) {
    auto ro = dynamic_cast<const UHDM::ref_obj *>(haystack);
    if (ro->Actual_group() == needle) {
      list->push_back(haystack);
    }
  }
}

} // namespace

void GetDriversOrLoads(const UHDM::any *item, bool drivers,
                       std::vector<const UHDM::any *> *list) {
  list->clear();
  if (item == nullptr) return;
  // For RefObjects, trace the actual net it's referring to.
  if (item->VpiType() == vpiRefObj) {
    item = dynamic_cast<const UHDM::ref_obj *>(item)->Actual_group();
  }
  if (!IsTraceable(item)) return;
  // First, find the containing module.
  auto m = GetContainingModule(item);
  // There should always be a containing module, but just in case:
  if (m == nullptr) return;
  RecurseFindItem(m, item, drivers, list);
  // Check to see if the net is a module input or inout.
  if (m->Ports() != nullptr) {
    for (auto p : *m->Ports()) {
      if (p->Low_conn()->VpiType() == vpiRefObj) {
        auto ro = dynamic_cast<const UHDM::ref_obj *>(p->Low_conn());
        // Inputs and inouts are drivers, outputs and inouts are loads.
        if (ro->Actual_group() == item &&
            (p->VpiDirection() == vpiInout ||
             p->VpiDirection() == (drivers ? vpiInput : vpiOutput))) {
          list->push_back(p);
        }
      }
    }
  }
}

const UHDM::module *GetContainingModule(const UHDM::any *item) {
  bool keep_going = false;
  do {
    if (keep_going && item->VpiType() == vpiModule) {
      keep_going = false;
    }
    auto prev_item = item;
    item = item->VpiParent();
    if (item->VpiType() == vpiPort) {
      auto p = dynamic_cast<const UHDM::port *>(item);
      if (p->High_conn() == prev_item) {
        // If the thing we were tracing was a connection to a port, then the
        // containing module is really the modules that contains this module's
        // instance.
        keep_going = true;
      }
    }
  } while (!(item == nullptr || (item->VpiType() == vpiModule && !keep_going)));
  return dynamic_cast<const UHDM::module *>(item);
}

const UHDM::any *GetScopeForUI(const UHDM::any *item) {
  while (!(item == nullptr || item->VpiType() == vpiModule ||
           item->VpiType() == vpiGenScopeArray)) {
    item = item->VpiParent();
  }
  return item;
}

bool IsTraceable(const UHDM::any *item) {
  const int type = item->VpiType();
  return type == vpiNet || type == vpiPort || type == vpiLogicVar ||
         type == vpiLongIntVar || type == vpiArrayVar || type == vpiArrayNet ||
         type == vpiShortIntVar || type == vpiIntVar ||
         type == vpiShortRealVar || type == vpiByteVar || type == vpiClassVar ||
         type == vpiStringVar || type == vpiEnumVar || type == vpiStructVar ||
         type == vpiUnionVar || type == vpiBitVar || type == vpiRefObj;
}

} // namespace sv
