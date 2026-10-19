// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/badge_cache.hpp>
#include <curl/curl.h>
#include <stb_image.h>
#include <chrono>
#include <cstddef>
#include <future>
#include <string>
#include <utility>
#include <vector>
namespace ravenemudsk_ui {
namespace {
constexpr size_t MAX_BYTES=768*1024;
bool valid_url(const std::string& url) {
    if(url.size()<10||url.size()>512)return false;
    CURLU* parsed=curl_url();
    if(!parsed)return false;
    bool valid=false;
    if(curl_url_set(parsed,CURLUPART_URL,url.c_str(),CURLU_NON_SUPPORT_SCHEME)==CURLUE_OK) {
        char *scheme=nullptr,*host=nullptr,*user=nullptr,*password=nullptr,*query=nullptr,
             *fragment=nullptr,*port=nullptr,*path=nullptr;
        const bool has_scheme=curl_url_get(parsed,CURLUPART_SCHEME,&scheme,0)==CURLUE_OK;
        const bool has_host=curl_url_get(parsed,CURLUPART_HOST,&host,0)==CURLUE_OK;
        const bool has_user=curl_url_get(parsed,CURLUPART_USER,&user,0)==CURLUE_OK;
        const bool has_password=curl_url_get(parsed,CURLUPART_PASSWORD,&password,0)==CURLUE_OK;
        const bool has_query=curl_url_get(parsed,CURLUPART_QUERY,&query,0)==CURLUE_OK;
        const bool has_fragment=curl_url_get(parsed,CURLUPART_FRAGMENT,&fragment,0)==CURLUE_OK;
        const bool has_port=curl_url_get(parsed,CURLUPART_PORT,&port,0)==CURLUE_OK;
        const bool has_path=curl_url_get(parsed,CURLUPART_PATH,&path,0)==CURLUE_OK;
        std::string hostname=host?host:"";
        const std::string suffix=".retroachievements.org";
        const bool allowed_host=hostname=="retroachievements.org" ||
            (hostname.size()>suffix.size() &&
             hostname.compare(hostname.size()-suffix.size(),suffix.size(),suffix)==0);
        valid=has_scheme && std::string(scheme)=="https" && has_host && allowed_host &&
            !has_user && !has_password && !has_query && !has_fragment &&
            (!has_port || std::string(port)=="443") && has_path &&
            std::string(path).find("..")==std::string::npos;
        for(char* value:{scheme,host,user,password,query,fragment,port,path})
            if(value)curl_free(value);
    }
    curl_url_cleanup(parsed);
    return valid;
}
size_t collect(char* data,size_t size,size_t count,void* user) {
    auto* bytes=static_cast<std::vector<std::uint8_t>*>(user);
    if(size && count>MAX_BYTES/size)return 0;
    const size_t length=size*count;
    if(length>MAX_BYTES-bytes->size())return 0;
    const auto* src=reinterpret_cast<const std::uint8_t*>(data);
    bytes->insert(bytes->end(),src,src+length);
    return length;
}
std::vector<std::uint8_t> download_image(const std::string& url) {
    std::vector<std::uint8_t> bytes;
    if(!valid_url(url))return {};
    CURL* curl=curl_easy_init();
    if(!curl)return {};
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,6L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,8L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"RavenEmuDSK/0.1.0");
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,collect);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&bytes);
    const CURLcode result=curl_easy_perform(curl);
    long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_cleanup(curl);
    if(result!=CURLE_OK || status!=200)return {};
    return bytes;
}
SDL_Texture* decode(SDL_Renderer* renderer,const std::vector<std::uint8_t>& data) {
    if(data.empty())return nullptr;
    int width=0,height=0,channels=0;
    if(!stbi_info_from_memory(data.data(),static_cast<int>(data.size()),
                              &width,&height,&channels))return nullptr;
    if(width<1||height<1||width>2048||height>2048)return nullptr;
    stbi_uc* pixels=stbi_load_from_memory(data.data(),static_cast<int>(data.size()),
                                            &width,&height,&channels,4);
    if(!pixels)return nullptr;
    SDL_Texture* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STATIC,width,height);
    if(texture && SDL_UpdateTexture(texture,nullptr,pixels,width*4)!=0) {
        SDL_DestroyTexture(texture);texture=nullptr;
    }
    if(texture)SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    stbi_image_free(pixels);
    return texture;
}
}
BadgeCache::~BadgeCache() {
    // Each future may block until its short HTTPS timeout; never free a live
    // SDL texture or renderer while an image upload is in progress.
    for(auto& pair:images_)if(pair.second.download.valid())pair.second.download.wait();
    for(auto& pair:images_)if(pair.second.texture)SDL_DestroyTexture(pair.second.texture);
}
SDL_Texture* BadgeCache::image(const std::string& url) {
    if(url.empty()||!valid_url(url))return nullptr;
    const auto found=images_.find(url);
    if(found==images_.end() && images_.size()>=128)return nullptr;
    auto& e=images_[url];
    if(e.finished)return e.texture;
    if(!e.started) {
        size_t downloading=0;
        for(const auto& item:images_)
            if(item.second.started&&!item.second.finished)++downloading;
        if(downloading>=2)return nullptr;
        e.download=std::async(std::launch::async,[url]{return download_image(url);});
        e.started=true;
    }
    if(e.download.valid() &&
       e.download.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        const auto bytes=e.download.get();
        e.texture=decode(renderer_,bytes);
        e.finished=true;
    }
    return e.texture;
}
} // namespace ravenemudsk_ui
