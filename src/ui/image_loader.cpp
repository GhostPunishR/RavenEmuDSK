// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/image_loader.hpp>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>
namespace ravenemudsk_ui {
namespace fs=std::filesystem;
SDL_Texture* load_local_image(SDL_Renderer* renderer,const fs::path& path){
    if(!renderer) return nullptr;
    std::error_code ec;
    if(!fs::is_regular_file(path,ec)||ec)return nullptr;
    const auto bytes=fs::file_size(path,ec);
    if(ec||bytes<16||bytes>8u*1024u*1024u)return nullptr;
    std::ifstream input(path,std::ios::binary);
    if(!input)return nullptr;
    std::vector<stbi_uc> contents(static_cast<std::size_t>(bytes));
    if(!input.read(reinterpret_cast<char*>(contents.data()),
                   static_cast<std::streamsize>(contents.size())))return nullptr;
    int w=0,h=0,channels=0;
    if(!stbi_info_from_memory(contents.data(),
          static_cast<int>(contents.size()),&w,&h,&channels))return nullptr;
    if(w<1||h<1||w>2048||h>2048)return nullptr;
    stbi_uc* rgba=stbi_load_from_memory(contents.data(),
                     static_cast<int>(contents.size()),&w,&h,&channels,4);
    if(!rgba)return nullptr;
    SDL_Texture* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STATIC,w,h);
    if(texture) {
        if(SDL_UpdateTexture(texture,nullptr,rgba,w*4)!=0){
            SDL_DestroyTexture(texture);
            texture=nullptr;
        }else SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    }
    stbi_image_free(rgba);
    return texture;
}
} // namespace ravenemudsk_ui

namespace ravenemudsk_ui {
SDL_Surface* load_local_surface(const std::filesystem::path& path) {
    std::error_code ec;
    if(!fs::is_regular_file(path,ec)||ec)return nullptr;
    const auto size=fs::file_size(path,ec);
    if(ec||size<16||size>8u*1024u*1024u)return nullptr;
    std::ifstream in(path,std::ios::binary);
    if(!in)return nullptr;
    std::vector<stbi_uc> bytes(static_cast<std::size_t>(size));
    if(!in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))return nullptr;
    int w=0,h=0,ch=0;
    stbi_uc* rgba=stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&w,&h,&ch,4);
    if(!rgba)return nullptr;
    SDL_Surface* icon=nullptr;
    if(w>0 && h>0 && w<=2048 && h<=2048) {
        icon=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
        if(icon) {
            for(int y=0;y<h;++y) {
                std::memcpy(static_cast<unsigned char*>(icon->pixels)+y*icon->pitch,
                            rgba+static_cast<size_t>(y)*w*4,static_cast<size_t>(w)*4);
            }
        }
    }
    stbi_image_free(rgba);
    return icon;
}

bool save_png(const std::filesystem::path& path, const std::uint32_t* argb, int width, int height) {
    if(!argb||width<1||height<1||width>4096||height>4096)return false;
    const auto count=static_cast<std::size_t>(width)*static_cast<std::size_t>(height);
    std::vector<unsigned char> rgb(count*3);
    for(std::size_t i=0;i<count;++i) {
        rgb[i*3]=static_cast<unsigned char>(argb[i]>>16);
        rgb[i*3+1]=static_cast<unsigned char>(argb[i]>>8);
        rgb[i*3+2]=static_cast<unsigned char>(argb[i]);
    }
    std::vector<unsigned char> png;
    const auto append=[](void* context,void* data,int size) {
        auto* out=static_cast<std::vector<unsigned char>*>(context);
        const auto* bytes=static_cast<const unsigned char*>(data);
        out->insert(out->end(),bytes,bytes+size);
    };
    if(!stbi_write_png_to_func(append,&png,width,height,3,rgb.data(),width*3))return false;
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    return out.write(reinterpret_cast<const char*>(png.data()),static_cast<std::streamsize>(png.size()))&&out.flush();
}
} // namespace ravenemudsk_ui
