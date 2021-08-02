#ifndef _SRC_UI_H_
#define _SRC_UI_H_

#include "absl/time/time.h"
#include "hierarchy.h"
#include "source.h"
#include "waves.h"
#include "workspace.h"
#include <uhdm/headers/design.h>
#include <memory>

namespace sv {

class UI {
 public:
  UI(UHDM::design *d);
  ~UI();

  void EventLoop();
  void AddIncludeDir(const std::string &dir);

 private:
  void DrawPanes(bool resize);

  int wave_pos_y_;
  int src_pos_x_;
  int term_w_;
  int term_h_;
  std::vector<int> tmp_ch; // TODO: remove
  absl::Time last_ch;

  std::unique_ptr<Hierarchy> hierarchy_;
  std::unique_ptr<Source> source_;
  std::unique_ptr<Waves> waves_;
  Panel *focused_panel_ = nullptr;
  Panel *prev_focused_panel_ = nullptr;
  Workspace workspace_;
};

} // namespace sv
#endif // _SRC_UI_H_
