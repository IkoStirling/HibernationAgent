#pragma once
#include <string>
namespace hibernation {
class DialoguePreparation {
    bool input_=false,pending_=false;double received_=0;
public:
    void input(bool active){input_=active;}
    void receive(double clock){pending_=true;received_=clock;}
    void finish(){pending_=false;}
    bool pending()const{return pending_;}
    std::string phase(double clock)const{return pending_?(clock-received_<.35?"receiving":"preparing"):input_?"receiving":"idle";}
};
}
