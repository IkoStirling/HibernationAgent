        if(kind=="runtime.attention"){const auto epoch=e.value("epoch",std::uint64_t(0));if(epoch!=attentionEpoch_||e.value("input_active",false))cancelPose();attentionEpoch_=epoch;attentionBusy_=e.value("busy",false);attentionInput_=e.value("input_active",false);poseQueue_.attention(epoch);dialogue_.input(attentionInput_);if(attentionBusy_||attentionInput_){cancelRest("attention_changed");cancelWorld("attention_changed");if(!dragged_&&!dropping_&&(state_=="idle"||state_=="curious")){state_="listening";stateUntil_=clock_+600;}}else if(state_=="listening"||((state_=="receiving"||state_=="preparing")&&!dialogue_.pending())){state_="idle";stateUntil_=clock_+2;}return;}
        if(kind=="habitat.navigate"&&e.value("plugin",std::string{})=="habitat"){
            const auto token=e.value("token",std::string{});const double x=e.value("x",-1.0),y=e.value("y",-1.0);
            if(token.empty()||token.size()>96||!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1)return;
            if(!visible_||paused_||pointer_.pressed()||dragged_||dropping_||poseQueue_.pending()||!heldPose_.empty()||!restToken_.empty()||clock_<interactionQuietUntil_){host_.emit({{"kind","widget.route_rejected"},{"plugin","widget"},{"target_token",token}});return;}
            cancelMovement("new_world_action");worldToken_=token;targetX_=x*(width_-size_);targetY_=y*(height_-size_);constrainPoint(targetX_,targetY_);state_="walk";stateUntil_=clock_+120;return;
        }
        if(e.value("plugin",std::string{})=="habitat"&&!worldToken_.empty()){
            if(kind=="habitat.action_started"&&e.value("token",std::string{})==worldToken_){heldItem_=e.value("action",std::string{})=="arrange"?PluginJson(nullptr):e.value("item",PluginJson(nullptr));state_=e.value("action",std::string{})=="eat"?"eating":"curious";stateUntil_=clock_+3;return;}
            if((kind=="habitat.consumed"||kind=="habitat.collected"||kind=="habitat.planted"||kind=="habitat.inspected"||kind=="habitat.arranged"||kind=="habitat.action_cancelled")&&e.value("token",std::string{})==worldToken_){worldToken_.clear();heldItem_=nullptr;postponeRoam();if(state_!="speaking"){state_="idle";stateUntil_=clock_+3;}return;}
        }
