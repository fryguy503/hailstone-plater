// SPDX-License-Identifier: MIT
// Standalone offline preview of Crest's production Lua painter. Dependencies:
// Lua 5.4, Dear ImGui, stb_image_write.h, and the C++20 standard library only.
// No Hailstone source headers, SDK, client objects, game assets or GPU are used.
// Usage: crest_preview <addon-root> <output.png> [font.ttf]
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <lua.hpp>
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace {
constexpr int kWidth=1760, kHeight=980;
constexpr float kCardWidth=400, kCardHeight=350;

float field(lua_State* L,int table,const char* key,float fallback=0) {
    lua_getfield(L,table,key);
    const float result=lua_isnumber(L,-1)?static_cast<float>(lua_tonumber(L,-1)):fallback;
    lua_pop(L,1);
    if (!std::isfinite(result)) throw std::runtime_error(std::string("Nonfinite canvas field: ")+key);
    return result;
}
std::string string_field(lua_State* L,int table,const char* key) {
    lua_getfield(L,table,key);
    std::size_t size=0;
    const char* value=lua_tolstring(L,-1,&size);
    std::string result=value?std::string(value,size):std::string();
    lua_pop(L,1);
    return result;
}
ImU32 rgb(std::uint32_t color,float alpha) {
    return IM_COL32((color>>16)&255,(color>>8)&255,color&255,static_cast<int>(std::clamp(alpha,0.0f,1.0f)*255.0f+0.5f));
}

// The same font supplies both the actual glyph ink measurements and draw text.
// Measuring overhang rather than only advance catches italic/descender clipping.
int measure(lua_State* L) {
    auto* font=static_cast<ImFont*>(lua_touserdata(L,lua_upvalueindex(1)));
    std::size_t bytes=0;
    const char* text=luaL_checklstring(L,1,&bytes);
    const float size=static_cast<float>(luaL_checknumber(L,2));
    if (!(size>=4 && size<=256) || bytes>1024) { lua_pushnil(L); return 1; }
    const ImVec2 advance=font->CalcTextSizeA(size,std::numeric_limits<float>::max(),0,text,text+bytes);
    const float scale=size/font->FontSize;
    float ink_x0=0,ink_y0=0,ink_x1=0,ink_y1=0,pen=0;
    bool ink=false;
    const char* at=text;
    while (at<text+bytes) {
        unsigned int codepoint=0;
        const int length=ImTextCharFromUtf8(&codepoint,at,text+bytes);
        if (length<=0) break;
        at+=length;
        const ImFontGlyph* glyph=font->FindGlyph(static_cast<ImWchar>(codepoint));
        if (!glyph) continue;
        if (glyph->Visible) {
            const float x0=pen+glyph->X0*scale,x1=pen+glyph->X1*scale;
            const float y0=glyph->Y0*scale,y1=glyph->Y1*scale;
            if (!ink) { ink_x0=x0; ink_y0=y0; ink_x1=x1; ink_y1=y1; ink=true; }
            else { ink_x0=std::min(ink_x0,x0); ink_y0=std::min(ink_y0,y0); ink_x1=std::max(ink_x1,x1); ink_y1=std::max(ink_y1,y1); }
        }
        pen+=glyph->AdvanceX*scale;
    }
    lua_createtable(L,0,6);
    const auto put=[&](const char* name,float value) { lua_pushnumber(L,value); lua_setfield(L,-2,name); };
    put("width",advance.x); put("height",advance.y);
    put("ink_x0",ink_x0); put("ink_y0",ink_y0); put("ink_x1",ink_x1); put("ink_y1",ink_y1);
    return 1;
}

// The public canvas fixes each rounded corner to sixteen segments; global
// ImGui curve-tessellation preferences must not alter the preview's geometry.
void rounded(ImDrawList& draw,ImVec2 lo,ImVec2 hi,float radius,ImU32 tint,bool filled,float thickness) {
    radius=std::max(0.0f,std::min({radius,(hi.x-lo.x)*0.5f,(hi.y-lo.y)*0.5f}));
    if (radius==0) {
        if (filled) draw.AddRectFilled(lo,hi,tint);
        else draw.AddRect(lo,hi,tint,0,0,thickness);
        return;
    }
    constexpr float pi=3.14159265358979323846f;
    const std::array<ImVec2,4> centers={ImVec2(lo.x+radius,lo.y+radius),ImVec2(hi.x-radius,lo.y+radius),
        ImVec2(hi.x-radius,hi.y-radius),ImVec2(lo.x+radius,hi.y-radius)};
    constexpr std::array<float,4> angles={pi,pi*1.5f,0,pi*0.5f};
    draw.PathClear();
    for (std::size_t corner=0;corner<centers.size();++corner) draw.PathArcTo(centers[corner],radius,angles[corner],angles[corner]+pi*0.5f,16);
    if (filled) draw.PathFillConvex(tint);
    else draw.PathStroke(tint,ImDrawFlags_Closed,thickness);
}

void primitive(lua_State* L,int command,ImDrawList& draw,ImFont* font,ImVec2 origin) {
    const std::string kind=string_field(L,command,"kind");
    const auto point=[&](const char* x,const char* y) { return ImVec2(origin.x+field(L,command,x),origin.y+field(L,command,y)); };
    const float alpha=field(L,command,"alpha",1),rounding=field(L,command,"rounding"),thickness=field(L,command,"thickness",1);
    const auto color=static_cast<std::uint32_t>(field(L,command,"color",16777215));
    const ImU32 tint=rgb(color,alpha);
    if (kind=="Rect") rounded(draw,point("x0","y0"),point("x1","y1"),rounding,tint,false,thickness);
    else if (kind=="RectFilled") rounded(draw,point("x0","y0"),point("x1","y1"),rounding,tint,true,thickness);
    else if (kind=="Gradient") {
        const ImVec2 top=point("x0","y0"),bottom=point("x1","y1");
        const auto lower=static_cast<std::uint32_t>(field(L,command,"color2",16777215));
        const float lower_alpha=field(L,command,"alpha2",alpha);
        if (rounding<=0) {
            const ImU32 other=rgb(lower,lower_alpha);
            draw.AddRectFilledMultiColor(top,bottom,tint,tint,other,other);
            return;
        }
        const int first=draw.VtxBuffer.Size;
        // Build white coverage first: a transparent top still has geometry for
        // an opaque lower color, and the antialias fringe keeps its coverage.
        rounded(draw,top,bottom,rounding,IM_COL32_WHITE,true,thickness);
        for (int i=first;i<draw.VtxBuffer.Size;++i) {
            auto& vertex=draw.VtxBuffer[i];
            const float t=std::clamp((vertex.pos.y-top.y)/std::max(0.0001f,bottom.y-top.y),0.0f,1.0f);
            const auto channel=[&](int shift) {
                const float a=static_cast<float>((color>>shift)&255),b=static_cast<float>((lower>>shift)&255);
                return static_cast<int>(std::lround(a+(b-a)*t));
            };
            const float coverage=static_cast<float>((vertex.col>>IM_COL32_A_SHIFT)&255)/255.0f;
            const float opacity=std::clamp(alpha+(lower_alpha-alpha)*t,0.0f,1.0f);
            vertex.col=IM_COL32(channel(16),channel(8),channel(0),static_cast<int>(std::lround(coverage*opacity*255.0f)));
        }
    } else if (kind=="Line") draw.AddLine(point("x0","y0"),point("x1","y1"),tint,thickness);
    else if (kind=="Circle") draw.AddCircle(point("x","y"),field(L,command,"radius"),tint,static_cast<int>(field(L,command,"segments",32)),thickness);
    else if (kind=="CircleFilled") draw.AddCircleFilled(point("x","y"),field(L,command,"radius"),tint,static_cast<int>(field(L,command,"segments",32)));
    else if (kind=="Triangle") draw.AddTriangle(point("x0","y0"),point("x1","y1"),point("x2","y2"),tint,thickness);
    else if (kind=="TriangleFilled") draw.AddTriangleFilled(point("x0","y0"),point("x1","y1"),point("x2","y2"),tint);
    else if (kind=="Text") {
        const std::string text=string_field(L,command,"text");
        draw.AddText(font,field(L,command,"size",18),point("x","y"),tint,text.c_str(),text.c_str()+text.size());
    } else if (kind=="Image") {
        // No user game assets are loaded in this fixture. The production renderer
        // receives nil from texture lookup and therefore emits its vector glyph.
        throw std::runtime_error("Unexpected image in asset-free preview");
    } else throw std::runtime_error("Unknown primitive: "+kind);
}

float edge(ImVec2 a,ImVec2 b,ImVec2 p) { return (p.x-a.x)*(b.y-a.y)-(p.y-a.y)*(b.x-a.x); }
bool inclusive_edge(ImVec2 a,ImVec2 b,float area) {
    if (area<0) std::swap(a,b);
    return a.y<b.y || (a.y==b.y && a.x>b.x);
}

// CPU triangle rasterizer: only ImGui's atlas is sampled. Shapes use its white
// texel. Pixel-center/scissor and half-open triangle edges preserve transparency.
std::vector<unsigned char> raster(const ImDrawData& data,const unsigned char* atlas,int aw,int ah) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(kWidth)*kHeight*4);
    for (std::size_t i=0;i<pixels.size();i+=4) { pixels[i]=13; pixels[i+1]=18; pixels[i+2]=28; pixels[i+3]=255; }
    const auto sample=[&](float u,float v) {
        const float x=std::clamp(u*static_cast<float>(aw)-0.5f,0.0f,static_cast<float>(aw-1));
        const float y=std::clamp(v*static_cast<float>(ah)-0.5f,0.0f,static_cast<float>(ah-1));
        const int x0=static_cast<int>(x),y0=static_cast<int>(y),x1=std::min(x0+1,aw-1),y1=std::min(y0+1,ah-1);
        const float fx=x-static_cast<float>(x0),fy=y-static_cast<float>(y0);
        const auto a=[&](int px,int py) { return static_cast<float>(atlas[(py*aw+px)*4+3])/255.0f; };
        return (a(x0,y0)*(1-fx)+a(x1,y0)*fx)*(1-fy)+(a(x0,y1)*(1-fx)+a(x1,y1)*fx)*fy;
    };
    for (int list=0;list<data.CmdListsCount;++list) {
        const ImDrawList& draw=*data.CmdLists[list];
        for (const ImDrawCmd& cmd:draw.CmdBuffer) {
            if (cmd.UserCallback) continue;
            for (unsigned int n=0;n+2<cmd.ElemCount;n+=3) {
                const auto vertex=[&](unsigned int index)->const ImDrawVert& { return draw.VtxBuffer[static_cast<int>(cmd.VtxOffset+draw.IdxBuffer[static_cast<int>(cmd.IdxOffset+index)])]; };
                const auto& a=vertex(n); const auto& b=vertex(n+1); const auto& c=vertex(n+2);
                const float area=edge(a.pos,b.pos,c.pos);
                if (std::abs(area)<0.00001f) continue;
                const int x0=std::max(0,static_cast<int>(std::floor(std::max(cmd.ClipRect.x,std::min({a.pos.x,b.pos.x,c.pos.x})))));
                const int y0=std::max(0,static_cast<int>(std::floor(std::max(cmd.ClipRect.y,std::min({a.pos.y,b.pos.y,c.pos.y})))));
                const int x1=std::min(kWidth,static_cast<int>(std::ceil(std::min(cmd.ClipRect.z,std::max({a.pos.x,b.pos.x,c.pos.x})))));
                const int y1=std::min(kHeight,static_cast<int>(std::ceil(std::min(cmd.ClipRect.w,std::max({a.pos.y,b.pos.y,c.pos.y})))));
                for (int y=y0;y<y1;++y) for (int x=x0;x<x1;++x) {
                    const ImVec2 at(static_cast<float>(x)+0.5f,static_cast<float>(y)+0.5f);
                    if (at.x<cmd.ClipRect.x || at.y<cmd.ClipRect.y || at.x>=cmd.ClipRect.z || at.y>=cmd.ClipRect.w) continue;
                    const float wa=edge(b.pos,c.pos,at)/area,wb=edge(c.pos,a.pos,at)/area,wc=edge(a.pos,b.pos,at)/area;
                    if (wa<0 || wb<0 || wc<0 || (wa==0 && !inclusive_edge(b.pos,c.pos,area)) ||
                        (wb==0 && !inclusive_edge(c.pos,a.pos,area)) || (wc==0 && !inclusive_edge(a.pos,b.pos,area))) continue;
                    const auto channel=[&](int shift) {
                        return static_cast<float>((a.col>>shift)&255)*wa+static_cast<float>((b.col>>shift)&255)*wb+static_cast<float>((c.col>>shift)&255)*wc;
                    };
                    const float alpha=channel(IM_COL32_A_SHIFT)/255.0f*sample(a.uv.x*wa+b.uv.x*wb+c.uv.x*wc,a.uv.y*wa+b.uv.y*wb+c.uv.y*wc);
                    const std::size_t pixel=(static_cast<std::size_t>(y)*kWidth+static_cast<std::size_t>(x))*4;
                    constexpr std::array<int,3> shifts={IM_COL32_R_SHIFT,IM_COL32_G_SHIFT,IM_COL32_B_SHIFT};
                    for (std::size_t ch=0;ch<3;++ch) pixels[pixel+ch]=static_cast<unsigned char>(std::clamp(channel(shifts[ch])*alpha+
                        static_cast<float>(pixels[pixel+ch])*(1-alpha),0.0f,255.0f));
                }
            }
        }
    }
    return pixels;
}

constexpr const char* kFixture=R"lua(
local C,E,R=require('crest.config'),require('crest.engine'),require('crest.renderer')
return function(metrics)
  local entries={}
  for i=1,8 do
    local title=C.presets[i]
    local s=C.preset(title)
    -- Shared synthetic data/colors only. Every structural field and visibility
    -- decision comes directly from the shipped preset and production engine.
    s.color_mode='custom'; s.npc_color=0xd95c64; s.mana_color=0x48a1de
    s.cast_color=0xd6b057; s.target_color=0xd5b36a; s.background_color=0x111821
    s.text_color=0xf2f5fa
    local spawn={id=7,name='Obsidian_Warden001',npc=true,class=1,level=65}
    local ctx={name='Obsidian Warden',target=true,distance=15,now_ms=0,dt_ms=16,hp_percent=72,mana_percent=36,
      casting={name='Obsidian Flame',elapsed_ms=1760,duration_ms=3200}}
    local extra={has_target=true,server_indicators={{icon='star',label='1',color=0xd5b36a},
      {icon='sleep',label='',color=0xd5b36a},{icon='circle',label='',color=0xd5b36a}}}
    local style=E.new():presentation(spawn,ctx,s,extra)
    local model,why=R.prepare(style,{name=ctx.name,target=true,font_size=18,font=0,scale=1},metrics)
    assert(model and not model.hide,why or 'Preview preset produced no visible model: '..title)
    assert(#model.commands<=R.MAX_COMMANDS and R.vertex_cost(model.commands)<=R.MAX_MODEL_VERTICES)
    entries[#entries+1]={title=title,description=C.descriptions[title],model=model}
  end
  return entries
end
)lua";

} // namespace

int main(int argc,char** argv) {
    if (argc<3 || argc>4) { std::cerr<<"Usage: crest_preview <addon-root> <output.png> [font.ttf]\n"; return 1; }
    try {
        const auto addon=std::filesystem::absolute(argv[1]);
        if (!std::filesystem::is_regular_file(addon/"crest"/"renderer.lua")) throw std::runtime_error("Crest renderer not found in addon root");
        if (argc==4 && !std::filesystem::is_regular_file(argv[3])) throw std::runtime_error("Font not found");
        std::unique_ptr<ImGuiContext,void(*)(ImGuiContext*)> context(ImGui::CreateContext(),ImGui::DestroyContext);
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
        io.DisplaySize=ImVec2(static_cast<float>(kWidth),static_cast<float>(kHeight)); io.DeltaTime=1.0f/60.0f;
        if (argc==4 && !io.Fonts->AddFontFromFileTTF(argv[3],18)) throw std::runtime_error("Font could not be loaded");
        unsigned char* atlas=nullptr; int aw=0,ah=0;
        io.Fonts->GetTexDataAsRGBA32(&atlas,&aw,&ah);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        ImGui::NewFrame();
        ImFont* font=ImGui::GetFont(); ImDrawList& draw=*ImGui::GetBackgroundDrawList();
        std::unique_ptr<lua_State,void(*)(lua_State*)> state(luaL_newstate(),lua_close);
        if (!state) throw std::runtime_error("Lua could not be initialized");
        lua_State* L=state.get(); luaL_openlibs(L);
        lua_getglobal(L,"package");
        const std::string paths=(addon/"?.lua").generic_string()+";"+(addon/"?"/"init.lua").generic_string();
        lua_pushlstring(L,paths.data(),paths.size()); lua_setfield(L,-2,"path"); lua_pop(L,1);
        const auto checked=[&](int result) { if (result!=LUA_OK) throw std::runtime_error(lua_tostring(L,-1)); };
        checked(luaL_loadstring(L,kFixture)); checked(lua_pcall(L,0,1,0));
        lua_newtable(L); lua_pushlightuserdata(L,font); lua_pushcclosure(L,measure,1); lua_setfield(L,-2,"measure");
        lua_pushcfunction(L,[](lua_State* args)->int { lua_pushnil(args); return 1; }); lua_setfield(L,-2,"texture");
        checked(lua_pcall(L,1,1,0));
        if (!lua_istable(L,-1) || lua_rawlen(L,-1)!=8) throw std::runtime_error("Expected eight actual Crest preset models");
        const int entries=lua_absindex(L,-1);
        const auto label=[&](float x,float y,const char* text,float size,ImU32 tint,float wrap=0) {
            draw.AddText(font,size,ImVec2(x,y),tint,text,nullptr,wrap);
        };
        label(44,25,"HAILSTONE CREST",32,IM_COL32(235,242,255,255));
        label(44,76,"Offline addon painter preview | Eight shipped presets",17,IM_COL32(146,163,185,255));
        label(44,104,"Production Lua renderer | Shared unit data and colors | Preset sizing and visibility preserved",14,IM_COL32(146,163,185,255));
        for (int i=1;i<=8;++i) {
            lua_rawgeti(L,entries,i); const int entry=lua_absindex(L,-1);
            const std::string title=string_field(L,entry,"title"),description=string_field(L,entry,"description");
            lua_getfield(L,entry,"model"); const int model=lua_absindex(L,-1);
            lua_getfield(L,model,"bounds"); const int bounds=lua_absindex(L,-1);
            const float bx=field(L,bounds,"x"),by=field(L,bounds,"y"),bw=field(L,bounds,"w"),bh=field(L,bounds,"h");
            lua_pop(L,1);
            if (bw>kCardWidth-36 || bh>230) throw std::runtime_error("Preset does not fit preview card: "+title);
            const float x=44+static_cast<float>((i-1)%4)*424,y=140+static_cast<float>((i-1)/4)*374;
            draw.AddRectFilled(ImVec2(x,y),ImVec2(x+kCardWidth,y+kCardHeight),IM_COL32(22,30,44,255),12);
            draw.AddRect(ImVec2(x,y),ImVec2(x+kCardWidth,y+kCardHeight),IM_COL32(49,64,85,255),12);
            label(x+22,y+20,title.c_str(),20,IM_COL32(132,192,248,255));
            label(x+22,y+296,description.c_str(),13,IM_COL32(146,163,185,255),kCardWidth-44);
            const ImVec2 origin(x+kCardWidth/2-bx-bw/2,y+65+(220-bh)/2-by);
            draw.PushClipRect(ImVec2(origin.x+bx,origin.y+by),ImVec2(origin.x+bx+bw,origin.y+by+bh),true);
            lua_getfield(L,model,"commands"); const int commands=lua_absindex(L,-1);
            const auto count=lua_rawlen(L,commands);
            for (std::size_t n=1;n<=count;++n) { lua_rawgeti(L,commands,static_cast<lua_Integer>(n)); primitive(L,lua_absindex(L,-1),draw,font,origin); lua_pop(L,1); }
            lua_pop(L,1); draw.PopClipRect(); lua_pop(L,2);
            std::cout<<title<<": "<<count<<" primitives, "<<bw<<" x "<<bh<<" pixels\n";
        }
        label(44,936,"Synthetic examples. Live gameplay and server acceptance remain untested.",14,IM_COL32(146,163,185,255));
        ImGui::Render();
        const auto pixels=raster(*ImGui::GetDrawData(),atlas,aw,ah);
        const auto output=std::filesystem::absolute(argv[2]);
        if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
        if (!stbi_write_png(output.string().c_str(),kWidth,kHeight,4,pixels.data(),kWidth*4)) throw std::runtime_error("PNG write failed");
        std::cout<<output.string()<<'\n'; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
