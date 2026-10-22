// SPDX-License-Identifier: GPL-3.0-or-later
#include "renderer.hpp"
#include "terminal-style.hpp"
#include "bdf_font.hpp"
#include <cassert>
#include <clocale>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include FT_FONT_FORMATS_H
int main(int argc,char **argv) {
 setlocale(LC_ALL,"C.UTF-8");unsetenv("DISPLAY");unsetenv("WAYLAND_DISPLAY");
 Fl_Term term(0,0,640,320,nullptr,true);term.grid(80,20);
 std::string data="\033[38;2;202;211;245;48;2;138;173;244mA\033[39mB\033[49mC"
                  "\033[38;5;196;48;5;234mD\033[0mE"
                  "\033[38:2::12:34:56;48:2:0:78:90:123mF\033[0m\r\n";
 for(char c:data)term.puts(&c,1);
 auto view=term.snapshot();
 auto cell=[&](std::string text){for(const auto &r:view.runs)if(r.text==text)return r;throw std::runtime_error("Missing cell: "+text);};
 using namespace terminal_style;
 assert((rgb(cell("A").style,true)==std::array<uint8_t,3>{202,211,245}));
 assert((rgb(cell("A").style,false)==std::array<uint8_t,3>{138,173,244}));
 assert(!(cell("B").style&foregroundSet) && (cell("B").style&backgroundRGB));
 assert(!(cell("C").style&(foregroundSet|backgroundSet)));
 assert((rgb(cell("D").style,true)==std::array<uint8_t,3>{255,0,0}));
 assert((rgb(cell("D").style,false)==std::array<uint8_t,3>{28,28,28}));
 assert(cell("E").style==7);
 assert((rgb(cell("F").style,true)==std::array<uint8_t,3>{12,34,56}));
 assert((rgb(cell("F").style,false)==std::array<uint8_t,3>{78,90,123}));
 assert(sgr(setRGB(7,true,{4,5,6}),"38;2;256;0;0")==setRGB(7,true,{4,5,6}));
 Fl_Term edge(0,0,640,320,nullptr,true);edge.grid(80,20);
 std::string border=std::string(78,'x')+"·\r\nZ";
 for(char c:border)edge.puts(&c,1);
 auto wrapped=edge.snapshot();bool complete=false,nextLine=false;
 for(const auto &r:wrapped.runs){complete|=r.row==0&&r.column==79&&r.text=="·";nextLine|=r.row==1&&r.column==0&&r.text=="Z";}
 assert(complete&&nextLine);
 Renderer renderer;renderer.font({Renderer::defaultFont(),16});renderer.opacity=1;renderer.render(term,0,1);
 auto pixel=[&](int x,int y){size_t at=(size_t(renderer.height-1-y)*renderer.width+x)*4;return std::array<uint8_t,3>{renderer.pixels[at],renderer.pixels[at+1],renderer.pixels[at+2]};};
 assert((pixel(0,0)==std::array<uint8_t,3>{138,173,244}));
 // Every private-use character in the user's current Starship configuration.
 const unsigned symbols[]={0xe0b0,0xe0b2,0xe213,0xe711,0xeeff,0xf001,0xf008,0xf019,0xf03e,0xf05c,0xf05d,0xf071,0xf0ad,0xf0da,0xf0f4,0xf113,0xf11b,0xf121,0xf19c,0xf240,0xf242,0xf243,0xf244,0xf29c,0xf303,0xf306,0xf30a,0xf30c,0xf315,0xf31a,0xf31b,0xf32a,0xf370,0xf380,0xf40e,0xf418,0xf468,0xf46e,0xf4a9,0xf4e2,0xf52c,0xf0219,0xf0239,0xf1105,0xf18f4};
 for(unsigned cp:symbols) {
  auto face=renderer.glyphFace(cp);
  if(!FT_Get_Char_Index(face,cp)){std::cerr<<"Missing BDF glyph U+"<<std::hex<<cp<<'\n';return 1;}
  assert(std::string(FT_Get_Font_Format(face))=="BDF");
 }
 bdf_fonts::Face base;assert(bdf_fonts::inspect(Renderer::defaultFont(),base));
 auto doubled=bdf_fonts::scaled(base,2);renderer.font({doubled.file,doubled.pixels});
 for(unsigned cp:symbols){auto f=renderer.glyphFace(cp);assert(FT_Get_Char_Index(f,cp));assert(std::string(FT_Get_Font_Format(f))=="BDF");}
 renderer.font({Renderer::defaultFont(),16});
 // Retain full RGB attributes in reallocated scrollback and DEC save/restore.
 term.disp("\033[38;2;1;2;3;48;2;4;5;6m\0337\033[0m\0338G\r\n");
 view=term.snapshot();assert((rgb(cell("G").style,true)==std::array<uint8_t,3>{1,2,3}));
 for(int i=0;i<5000;++i)term.disp("\033[38;2;10;20;30;48;2;40;50;60mRGB\033[0m\r\n");
 term.scrollRows(-100000);view=term.snapshot();assert((rgb(cell("A").style,false)==std::array<uint8_t,3>{138,173,244}));
 Terminal child(80,20,{"/bin/sh","-c","printf 'PROFILE_%s_%s\\n' \"$TERM\" \"$COLORTERM\""});
 auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);bool found=false;
 while(std::chrono::steady_clock::now()<end){child.pty->pump();child.selectAll();if(child.selectionText().find("PROFILE_xterm-256color_truecolor")!=std::string::npos){found=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 assert(found);
 if(argc>1){renderer.render(term,0,1);std::ofstream out(argv[1],std::ios::binary);out<<"P6\n"<<renderer.width<<' '<<renderer.height<<"\n255\n";for(int y=0;y<renderer.height;y++)for(int x=0;x<renderer.width;x++){size_t at=(size_t(renderer.height-1-y)*renderer.width+x)*4;out.write((char*)renderer.pixels.data()+at,3);}}
 std::cout<<"PASS: split RGB/256/colon SGR, resets, scrollback/save-restore, 45 Starship symbols using BDF only, xterm-256color/truecolor PTY\n";
}
