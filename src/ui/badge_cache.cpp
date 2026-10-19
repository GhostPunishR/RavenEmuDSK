// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/badge_cache.hpp>
#include <curl/curl.h>
#include <stb_image.h>
#include <chrono>
#include <algorithm>
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
    if(data.empty() || data.size()>MAX_BYTES)return nullptr;
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
BadgeCache::BadgeCache(SDL_Renderer* renderer, Download download, Clock clock, std::size_t capacity)
    : renderer_(renderer), download_(download ? std::move(download) : Download(download_image)),
      clock_(clock ? std::move(clock) : Clock([] { return SDL_GetTicks64(); })),
      capacity_(std::clamp<std::size_t>(capacity, 2, 128)) {}
BadgeCache::~BadgeCache() {
    for(auto& pair:images_)if(pair.second.download.valid())pair.second.download.wait();
    for(auto& pair:images_)if(pair.second.texture)SDL_DestroyTexture(pair.second.texture);
}
void BadgeCache::complete(Entry& e) {
    if (!e.download.valid() || e.download.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    std::vector<std::uint8_t> bytes;
    try { bytes = e.download.get(); } catch (...) { /* Retry transport failures too. */ }
    e.texture = decode(renderer_, bytes);
    if (!e.texture) {
        e.retry_at = clock_() + (2000ULL << std::min(e.failures++, 5u));
    } else e.failures = 0;
}
void BadgeCache::begin_frame() {
    ++frame_;
    for (auto& [url, e] : images_) complete(e);
}
SDL_Texture* BadgeCache::image(const std::string& url) {
    if(url.empty()||!valid_url(url))return nullptr;
    auto found=images_.find(url);
    if(found==images_.end()) {
        if (images_.size()>=capacity_) {
            auto oldest=images_.end();
            for (auto i=images_.begin();i!=images_.end();++i) {
                if(i->second.frame==frame_ || i->second.download.valid())continue;
                if(oldest==images_.end() || i->second.last_used<oldest->second.last_used)oldest=i;
            }
            if(oldest==images_.end())return nullptr;
            if(oldest->second.texture)SDL_DestroyTexture(oldest->second.texture);
            images_.erase(oldest);
        }
        found=images_.try_emplace(url).first;
    }
    auto& e=found->second;
    e.frame=frame_;e.last_used=++access_;
    complete(e);
    if(e.texture)return e.texture;
    if(!e.download.valid() && clock_()>=e.retry_at) {
        size_t downloading=0;
        for(const auto& item:images_)if(item.second.download.valid())++downloading;
        if(downloading<2) {
            e.download=std::async(std::launch::async,[send=download_,url]{return send(url);});
        }
    }
    return nullptr;
}
} // namespace ravenemudsk_ui
