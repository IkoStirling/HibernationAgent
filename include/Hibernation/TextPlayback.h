#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace hibernation {
// UTF-8 offsets ensure neither pagination nor reveal splits an encoded scalar.
inline std::vector<std::size_t> textOffsets(std::string_view text){
    std::vector<std::size_t> out{0};for(std::size_t at=0;at<text.size();){++at;while(at<text.size()&&(static_cast<unsigned char>(text[at])&0xc0)==0x80)++at;out.push_back(at);}return out;
}
inline std::vector<std::string> textPages(const std::string& text,std::size_t maximum){
    if(text.empty())return {};if(!maximum)return {text};const auto offsets=textOffsets(text);const auto count=offsets.size()-1;std::vector<std::string> pages;
    for(std::size_t start=0;start<count;){const auto cap=std::min(count,start+maximum);auto end=cap;std::size_t sentence=0,soft=0;
        for(auto i=start;i<cap;++i){const auto c=std::string_view(text).substr(offsets[i],offsets[i+1]-offsets[i]);if(c=="。"||c=="！"||c=="？"||c=="!"||c=="?"||c=="\n"||(c=="."&&(i+1==count||text[offsets[i+1]]==' ')))sentence=i+1;else if(c=="，"||c=="；"||c==","||c==";"||c==" ")soft=i+1;}
        if(cap<count){if(sentence>start)end=sentence;else if(soft>start+maximum/3)end=soft;}
        pages.push_back(text.substr(offsets[start],offsets[end]-offsets[start]));start=end;
    }return pages;
}
class TextPlayback {
    std::vector<std::string> pages_;std::vector<std::size_t> offsets_;std::size_t page_=0,visible_=0;double credit_=0,hold_=0,rate_=32,holdSeconds_=1.2;
    void select(){offsets_=textOffsets(pages_[page_]);visible_=0;credit_=0;hold_=0;}
public:
    void reset(const std::string& text,std::size_t maximum=0,double rate=32,double hold=1.2){pages_=textPages(text,maximum);page_=visible_=0;credit_=hold_=0;rate_=std::clamp(rate,1.0,120.0);holdSeconds_=std::clamp(hold,0.2,10.0);offsets_.clear();if(!pages_.empty())select();}
    void clear(){pages_.clear();offsets_.clear();page_=visible_=0;credit_=hold_=0;}
    bool tick(double seconds){if(pages_.empty()||seconds<=0)return false;const auto oldPage=page_,oldVisible=visible_;const auto count=offsets_.size()-1;
        if(visible_<count){credit_+=seconds*rate_;const auto added=static_cast<std::size_t>(credit_);visible_=std::min(count,visible_+added);credit_-=added;}
        else if(page_+1<pages_.size()){hold_+=seconds;if(hold_>=holdSeconds_){++page_;select();}}
        return oldPage!=page_||oldVisible!=visible_;
    }
    std::string visibleText()const{return pages_.empty()?std::string{}:pages_[page_].substr(0,offsets_[visible_]);}
    bool finished()const{return pages_.empty()||(page_+1==pages_.size()&&visible_+1==offsets_.size());}
    std::size_t pageIndex()const{return page_;}std::size_t pageCount()const{return pages_.size();}
};
}
