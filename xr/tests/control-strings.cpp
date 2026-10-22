// SPDX-License-Identifier: GPL-3.0-or-later
#include "renderer.hpp"
#include <cassert>
#include <clocale>
#include <fstream>
#include <iostream>
std::string visible(Fl_Term &term) {
 std::string text;for(const auto &run:term.snapshot().runs)text+=run.text;return text;
}
int main(int argc,char **argv) {
 setlocale(LC_ALL,"C.UTF-8");unsetenv("DISPLAY");unsetenv("WAYLAND_DISPLAY");
 const std::string controls="\033]0;BEL title\007"
   "\033]3008;start=metadata:type=prompt;command=private\033\\"
   "\033]133;A\033\\\033]7;file://host/path\007"
   "\033]8;;https://example.invalid/\033\\LINK\033]8;;\033\\"
   "\033P$qignored reply\033\\\033_Xprivate graphics metadata\033\\"
   "\033^private message\033\\\033Xprivate string\033\\"
   "\033]2;ST title\033\\";
 for(size_t chunk:{size_t(1),size_t(2),size_t(7),controls.size()}) {
  Fl_Term term(0,0,640,320,nullptr,true);term.grid(80,20);term.callback([](Fl_Widget*,void*){});
  term.disp("BEFORE");for(size_t i=0;i<controls.size();i+=chunk)term.puts(controls.data()+i,std::min(chunk,controls.size()-i));term.disp("AFTER");
  assert(visible(term)=="BEFORELINKAFTER");assert(std::string(term.title())=="ST title");
 }
 Fl_Term bounded(0,0,640,320,nullptr,true);bounded.grid(80,20);bounded.callback([](Fl_Widget*,void*){});
 std::string longString="\033]3008;"+std::string(16000,'x')+"\033\\OK";
 for(char c:longString)bounded.puts(&c,1);assert(visible(bounded)=="OK");
 bounded.disp("\033]3008;cancelled\030C\033Pcancelled\032D");assert(visible(bounded)=="OKCD");
 bounded.disp("\033[?9999~E");assert(visible(bounded)=="OKCDE");
 if(argc>1) {
  std::ifstream file(argv[1],std::ios::binary);assert(file);
  std::string startup((std::istreambuf_iterator<char>(file)),{});
  Fl_Term term(0,0,800,480,nullptr,true);term.grid(100,30);term.callback([](Fl_Widget*,void*){});
  for(char c:startup)term.puts(&c,1);
  auto text=visible(term);assert(text.find("start=")==std::string::npos);assert(text.find("command=")==std::string::npos);assert(text.find("")!=std::string::npos);
  std::cout<<"Captured real Bash/Starship startup contains no leaked OSC 3008 metadata\n";
 }
 std::cout<<"PASS: OSC 3008/133/7/8, BEL/ST titles, DCS/APC/PM/SOS, cancellation, bounds and fragmented reads\n";
}
