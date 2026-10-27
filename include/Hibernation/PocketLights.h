#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <string>
namespace hibernation {
// A finite virtual toy. Only legal presses change the board; a model cannot
// assign the board, move counter, win status or accumulated results.
class PocketLights {
public:
    using Json=nlohmann::json;
    bool active()const{return game_.is_object()&&game_.at("status")=="active";}
    const Json& game()const{return game_;}
    static unsigned pressMask(int cell){
        if(cell<0||cell>8)return 0;
        const int row=cell/3,column=cell%3;unsigned mask=0;
        for(const auto offset:std::array<std::array<int,2>,5>{{{0,0},{1,0},{-1,0},{0,1},{0,-1}}}){
            const int y=row+offset[0],x=column+offset[1];if(x>=0&&x<3&&y>=0&&y<3)mask|=1u<<(y*3+x);
        }return mask;
    }
    std::string start(std::uint32_t seed,int scramble,int limit){
        if(active())return "pocket_already_playing";if(sequence_>=1000000||scramble<2||scramble>8||limit<12||limit>64)return "pocket_game_limit";
        if(!seed)seed=0x9e3779b9u;std::array<int,9> cells={0,1,2,3,4,5,6,7,8};
        const auto random=[&seed]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
        for(int i=8;i>0;--i)std::swap(cells[i],cells[random()%static_cast<unsigned>(i+1)]);
        unsigned board=0;for(int i=0;i<scramble;++i)board^=pressMask(cells[i]);if(!board)board=pressMask(cells[0]);
        ++sequence_;game_={{"id","lights-"+std::to_string(sequence_)},{"revision",1},{"status","active"},{"board",board},{"moves",0},{"limit",limit}};return {};
    }
    std::string press(const std::string& id,std::int64_t revision,int cell){
        if(!game_.is_object()||game_.at("id")!=id)return "pocket_game_changed";
        if(game_.at("revision")!=revision)return "pocket_revision_conflict";
        if(!active())return "pocket_not_playing";if(cell<0||cell>8)return "pocket_cell";
        game_["board"]=game_.at("board").get<unsigned>()^pressMask(cell);game_["moves"]=game_.at("moves").get<int>()+1;game_["revision"]=revision+1;
        if(game_.at("board")==0){game_["status"]="won";++completed_;}
        else if(game_.at("moves")==game_.at("limit")){game_["status"]="exhausted";++left_;}return {};
    }
    std::string leave(const std::string& id,std::int64_t revision){
        if(!game_.is_object()||game_.at("id")!=id)return "pocket_game_changed";
        if(game_.at("revision")!=revision)return "pocket_revision_conflict";
        if(!active())return "pocket_not_playing";game_["status"]="left";game_["revision"]=revision+1;++left_;return {};
    }
    Json observe()const{
        Json view=game_;if(view.is_object()){
            const unsigned board=view.at("board");view.erase("board");view["rows"]=Json::array();
            for(int y=0;y<3;++y){Json row=Json::array();for(int x=0;x<3;++x)row.push_back((board&(1u<<(y*3+x)))!=0);view["rows"].push_back(std::move(row));}
            view["lit_count"]=std::popcount(board);view["moves_remaining"]=view.at("limit").get<int>()-view.at("moves").get<int>();
        }
        return {{"game",view},{"games_played",sequence_},{"games_finished",completed_},{"games_left",left_},{"rules","九盏虚拟灯排成3×3。cell=行×3+列，从左上0到右下8。按一格会翻转本格和上下左右存在的邻格，目标全部熄灭。可以自己考虑几步、试一次、改主意或放下；不是给用户派任务，不需要每次汇报。true表示亮灯。"}};
    }
    Json snapshot()const{return {{"version",1},{"sequence",sequence_},{"completed",completed_},{"left",left_},{"game",game_}};}
    bool restore(const Json& value){
        try{
            if(!value.is_object()||value.size()!=5||value.at("version")!=1)return false;
            const auto number=[](const Json& v,std::int64_t low,std::int64_t high){if(!v.is_number_integer())throw 0;const auto n=v.get<std::int64_t>();if(n<low||n>high)throw 0;return n;};
            const auto sequence=number(value.at("sequence"),0,1000000),completed=number(value.at("completed"),0,sequence),left=number(value.at("left"),0,sequence);
            if(completed+left>sequence)return false;const auto game=value.at("game");
            if(game.is_null()){if(sequence)return false;}
            else{
                if(!game.is_object()||game.size()!=6||game.at("id")!="lights-"+std::to_string(sequence)||!sequence)return false;
                const auto board=number(game.at("board"),0,511),limit=number(game.at("limit"),12,64),moves=number(game.at("moves"),0,limit),revision=number(game.at("revision"),1,67);
                const auto status=game.at("status").get<std::string>();if(status!="active"&&status!="won"&&status!="left"&&status!="exhausted")return false;
                if((status=="won")!=(board==0)||(status=="won"&&moves==0)||(status=="active"&&moves>=limit)||(status=="exhausted"&&moves!=limit))return false;
                if(revision!=moves+1+(status=="left"?1:0)||completed+left!=sequence-(status=="active"?1:0))return false;
            }
            sequence_=sequence;completed_=completed;left_=left;game_=game;return true;
        }catch(...){return false;}
    }
private:
    std::int64_t sequence_=0,completed_=0,left_=0;Json game_=nullptr;
};
}
