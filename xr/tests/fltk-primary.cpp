// SPDX-License-Identifier: GPL-3.0-or-later
// Independent FLTK client: exercises toolkit APIs, not FLTerm's clipboard
// helper.
#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Window.H>
#include <fstream>
#include <string>
class Input : public Fl_Input {
  std::string output_;

public:
  Input(const char *path) : Fl_Input(20, 30, 500, 40), output_(path) {
    maximum_size(8 * 1024 * 1024);
  }
  int handle(int event) override {
    if (event == FL_KEYDOWN && Fl::event_key() == FL_F + 1) {
      value("FLTK_PRIMARY_Žluťoučký_日本語");
      position(size(), 0);
      copy(0);
      return 1;
    }
    if (event == FL_KEYDOWN && Fl::event_key() == FL_F + 2) {
      value("");
      Fl::paste(*this, 0);
      return 1;
    }
    std::string pasted;
    if (event == FL_PASTE)
      pasted.assign(Fl::event_text(), Fl::event_length());
    int result = Fl_Input::handle(event);
    if (event == FL_PASTE)
      std::ofstream(output_, std::ios::binary) << pasted;
    return result;
  }
};
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  Fl_Window win(540, 100, "FLTK PRIMARY probe");
  win.xclass("org.domed.PrimaryProbe");
  Input input(argv[1]);
  win.end();
  win.show();
  input.take_focus();
  return Fl::run();
}
