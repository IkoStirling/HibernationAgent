        if(kind=="rhythm.pose.request"&&e.value("plugin",std::string{})=="rhythm"){
            const auto token=e.value("token",std::string{});const double seconds=e.value("seconds",0.0);
            if(token.empty()||token.size()>96||!std::isfinite(seconds)||seconds<15||seconds>1800)return;
            if(!visible_||paused_||dragged_||!worldToken_.empty()||!restToken_.empty()||clock_<interactionQuietUntil_||(state_!="idle"&&state_!="curious"))return;
            restToken_=token;state_="resting";stateUntil_=clock_+seconds+3;targetX_=x_;targetY_=y_;host_.emit({{"kind","widget.rest_started"},{"plugin","widget"},{"token",token}});positionEvent(true);invalidate();return;
        }
        if(kind=="rhythm.pose.release"&&e.value("plugin",std::string{})=="rhythm"&&e.value("token",std::string{})==restToken_){
            restToken_.clear();if(state_=="resting"){state_="idle";stateUntil_=clock_+3;}positionEvent(true);invalidate();return;
        }
