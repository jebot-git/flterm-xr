// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
namespace terminal_style {
using Style = uint64_t;
constexpr Style backgroundSet = 0x100, foregroundSet = 0x200;
constexpr Style foregroundRGB = 0x400, backgroundRGB = 0x800, inverse = 0x1000;
constexpr Style faint = 0x2000;
constexpr Style foregroundMask = Style(0xffffff) << 16, backgroundMask = Style(0xffffff) << 40;
inline constexpr std::array<std::array<uint8_t, 3>, 16> ansi{{
 {0,0,0},{192,0,0},{0,192,0},{192,192,0},{32,96,192},{192,0,192},{0,192,192},{192,192,192},
 {80,80,80},{255,80,80},{80,255,80},{255,255,80},{100,140,255},{255,80,255},{80,255,255},{255,255,255}}};
inline std::array<uint8_t, 3> indexed(int n) {
 if(n<16)return ansi[n];
 if(n>=232){auto v=uint8_t(8+(n-232)*10);return {v,v,v};}
 n-=16;auto channel=[](int v){return uint8_t(v?55+v*40:0);};
 return {channel(n/36),channel(n/6%6),channel(n%6)};
}
inline Style setRGB(Style style, bool foreground, std::array<uint8_t,3> color) {
 Style rgb=(Style(color[0])<<16)|(Style(color[1])<<8)|color[2];
 return (style & ~(foreground?foregroundMask:backgroundMask)) |
        (rgb << (foreground?16:40)) | (foreground?foregroundSet|foregroundRGB:backgroundSet|backgroundRGB);
}
inline std::array<uint8_t,3> rgb(Style style,bool foreground) {
 auto value=style>>(foreground?16:40);return {uint8_t(value>>16),uint8_t(value>>8),uint8_t(value)};
}
inline Style sgr(Style style,std::string_view input) {
 // Keep colon subparameters grouped: 38:2::r:g:b must not turn an omitted
 // colour-space ID into SGR 0 or consume a later, independent attribute.
 std::vector<std::vector<int>> groups(1,std::vector<int>(1,-1));
 for(char c:input) {
  if(c==';')groups.push_back({-1});
  else if(c==':')groups.back().push_back(-1);
  else if(c>='0'&&c<='9'){auto &n=groups.back().back();n=n<0?c-'0':(n>100000?100000:n*10+c-'0');}
  else return style;
 }
 for(size_t i=0;i<groups.size();++i) {
  int code=groups[i][0]<0?0:groups[i][0];
  if(code==38||code==48) {
   auto args=groups[i];
   if(args.size()==1) {
    if(i+1>=groups.size())continue;
    int mode=groups[i+1][0];size_t count=mode==5?2:mode==2?4:1;
    for(size_t n=0;n<count && i+1<groups.size();++n)args.push_back(groups[++i][0]);
   }
   if(args.size()==3&&args[1]==5&&args[2]>=0&&args[2]<=255)
    style=setRGB(style,code==38,indexed(args[2]));
   else if(args.size()>=5&&args[1]==2) {
    // Colon RGB may include an empty or zero colour-space identifier.
    size_t first=args.size()==6?3:2;
    if(first+3!=args.size() || (first==3 && args[2]>0))continue;
    int r=args[first],g=args[first+1],b=args[first+2];
    if(r>=0&&r<=255&&g>=0&&g<=255&&b>=0&&b<=255)
     style=setRGB(style,code==38,{uint8_t(r),uint8_t(g),uint8_t(b)});
   }
  } else if(code==0)style=7;
  else if(code==1)style|=8;
  else if(code==2)style|=faint;
  else if(code==22)style&=~(Style(8)|faint);
  else if(code==7)style|=inverse;
  else if(code==27)style&=~inverse;
  else if(code==39)style=(style & ~(foregroundSet|foregroundRGB|foregroundMask|Style(15)))|7;
  else if(code==49)style&=~(backgroundSet|backgroundRGB|backgroundMask|Style(0xf0));
  else if((code>=30&&code<=37)||(code>=90&&code<=97))
   style=(style & ~(foregroundRGB|foregroundMask|Style(15))) | foregroundSet | (code>=90?code-90+8:code-30);
  else if((code>=40&&code<=47)||(code>=100&&code<=107))
   style=(style & ~(backgroundRGB|backgroundMask|Style(0xf0))) | backgroundSet | Style(code>=100?code-100+8:code-40)<<4;
 }
 return style;
}
} // namespace terminal_style
