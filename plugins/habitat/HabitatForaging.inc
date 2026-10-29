    std::vector<bsimmer::supplies::Region> regions_;
    PluginJson regionConfig_=PluginJson::array(),gather_=nullptr,lastGather_=nullptr,menu_=PluginJson::array(),pantry_=PluginJson::array();
    std::map<std::string,double> nextResources_;bool inventoryReady_=false;
    std::string regionAt(double x,double y)const{for(const auto& r:regions_)if(r.contains(x,y))return r.id;return {};}
    void updateRegions(){for(const auto& item:world_.objects())world_.region(item.at("id"),regionAt(item.at("x"),item.at("y")));}
    void finishGather(const std::string& reason){if(gather_.is_null())return;gather_["status"]=reason;gather_["completed"]=reason=="completed";gather_["partial"]=gather_.at("collected").get<int>()>0&&reason!="completed";lastGather_=gather_;host_.emit({{"plugin","habitat"},{"kind","habitat.gather_finished"},{"gather",gather_},{"occurred_ms",nowMs()}});gather_=nullptr;}
    std::vector<bsimmer::supplies::GatherTarget> gatherTargets()const{std::vector<bsimmer::supplies::GatherTarget> out;for(const auto& item:world_.objects()){
        const auto resource=supplies::resource(item);if(resource.empty())continue;
        out.push_back({item.at("id"),item.value("region",std::string{}),resource,item.at("x"),item.at("y"),item.at("expires").get<double>()-world_.clock()});
    }return out;}
    PluginJson startGather(const PluginJson& args){
        if(!inventoryReady_||!canteenSupplies_)return toolFailure("resource_inventory_unavailable");
        if(paused_||!visible_||actorResting_||!actor_||!host_.desktopEnabled)return toolFailure("world_character_unavailable");
        if(world_.pending()||!gather_.is_null())return toolFailure("world_action_busy");
        const auto region=args.value("region",std::string{});if(!region.empty()&&std::none_of(regions_.begin(),regions_.end(),[&](const auto& r){return r.id==region;}))return toolFailure("resource_region_unknown");
        const std::string ingredient=args.value("ingredient_id",std::string{}),recipe=args.value("recipe_id",std::string{});
        if(!ingredient.empty()&&!recipe.empty())return toolFailure("gather_filter_conflict");
        std::map<std::string,unsigned> needs;int count=args.value("count",3);
        if(!ingredient.empty()){if(!supplies::known(ingredient))return toolFailure("resource_unknown");needs[ingredient]=count;}
        if(!recipe.empty()){
            bool found=false;for(const auto& r:menu_)if(r.at("id")==recipe){found=true;for(const auto& id:r.at("ingredients"))++needs[id.get<std::string>()];}
            if(!found)return toolFailure("canteen_recipe_unknown");
            // Existing warehouse materials already satisfy this goal. Bag contents
            // remain bag contents until an explicit store command, avoiding hidden moves.
            for(const auto& item:pantry_){const std::string id=item.at("id");if(needs.contains(id))needs[id]-=std::min(needs[id],item.at("available").get<unsigned>());}
            count=0;for(const auto& [id,n]:needs)count+=n;
        }
        if(count<0||count>8)return toolFailure("gather_quantity_invalid");
        const auto id=nextId("gather");gather_={{"id",id},{"region",region},{"needs",needs},{"requested",count},{"collected",0},{"results",PluginJson::array()},{"phase","finding"},{"deadline",world_.clock()+180},{"destination",args.value("destination",std::string(recipe.empty()?"backpack":"pantry"))}};
        if(count==0){finishGather("completed");return toolSuccess({{"completed",true},{"gather_id",id},{"collected",0}});}
        return toolSuccess({{"accepted",true},{"completed",false},{"gather_id",id},{"requested",count}});
    }
    void advanceGather(){
        if(gather_.is_null())return;
        if(world_.clock()>=gather_.at("deadline").get<double>()){world_.cancel("gather_timeout");finishGather("timed_out");return;}
        if(paused_||busy_||input_||!visible_||actorResting_||!actor_||!inventoryReady_){world_.cancel("gather_interrupted");finishGather("interrupted");return;}
        if(world_.pending())return;
        const auto needs=gather_.at("needs").get<std::map<std::string,unsigned>>();
        if(gather_.at("collected").get<int>()>=gather_.at("requested").get<int>()){finishGather("completed");return;}
        const auto target=bsimmer::supplies::nearest(gatherTargets(),actorX_,actorY_,gather_.at("region"),needs);
        if(target.empty()){finishGather("no_matching_resource");return;}
        const auto error=request(target,"collect");if(!error.empty()){finishGather(error);return;}
        gather_["phase"]="collecting";gather_["target"]=target;
    }
    void spawnResources(){
        if(!cfg_.value("resourceSpawn",cfg_.value("autoSpawn",true))||!canteenSupplies_||!inventoryReady_)return;
        for(const auto& r:regions_){auto& next=nextResources_[r.id];if(next==0)next=world_.clock()+5;
            if(world_.clock()<next)continue;next=world_.clock()+r.refreshSeconds;
            unsigned existing=0;for(const auto& item:world_.objects())if(item.value("region",std::string{})==r.id&&item.value("provenance",PluginJson::object()).value("type",std::string{})=="field_resource")++existing;
            if(existing>=r.capacity||world_.size()>=static_cast<std::size_t>(cfg_.value("maxObjects",6)))continue;
            auto areas=desktopActivityAreas(width_,height_,actorSize_);const double w=std::max(1,width_-actorSize_),h=std::max(1,height_-actorSize_);
            for(auto& a:areas){a.left=std::max(a.left,r.left*w);a.right=std::min(a.right,r.right*w);a.top=std::max(a.top,r.top*h);a.bottom=std::min(a.bottom,r.bottom*h);}
            std::erase_if(areas,[](const auto& a){return a.left>=a.right||a.top>=a.bottom;});if(areas.empty())continue;
            const auto [px,py]=sampleActivityPoint(areas,random_);const auto ingredient=r.resources.at(random_()%r.resources.size()),kind=supplies::templateFor(ingredient),id=nextId("resource");
            if(world_.placeDetailed(id,kind,px/w,py/h,{{"name",supplies::name(ingredient)},{"description","可收集的虚拟食材"}},std::uniform_real_distribution<double>(lifetimeMin_,lifetimeMax_)(random_),0,"region",{{"type","field_resource"},{"ingredient_id",ingredient},{"source_region",r.id}}))world_.region(id,r.id);
        }
    }
    class PickupHost final:public ayt::inventory::IInteractionHost {
        Habitat& owner_;std::string token_;
    public:
        PickupHost(Habitat& owner,std::string token):owner_(owner),token_(std::move(token)){}
        ayt::inventory::Result<void> validateCurrent(const ayt::inventory::InteractionCandidate& c,std::string_view actor)override{
            const auto pending=owner_.world_.action(),item=owner_.world_.object(c.objectId);
            if(actor!="character"||owner_.paused_||owner_.busy_||owner_.input_||!owner_.visible_||!owner_.actor_||nowMs()-owner_.actorSeen_>=2500||item.is_null()||pending.is_null()||pending.at("token")!=token_||pending.at("phase")!="awaiting_inventory"||pending.at("id")!=c.objectId||c.subjectId!="field/"+c.objectId||drops::fading(item,owner_.world_.clock())||std::hypot(item.at("x").get<double>()-owner_.actorX_,item.at("y").get<double>()-owner_.actorY_)>.03)return std::unexpected(ayt::inventory::Diagnostic{ayt::inventory::Error::Stale,"resource_no_longer_in_reach"});return {};
        }
        ayt::inventory::Result<void> authorize(const ayt::inventory::InteractionCandidate& c,std::string_view)override{
            if(supplies::resource(owner_.world_.object(c.objectId)).empty())return std::unexpected(ayt::inventory::Diagnostic{ayt::inventory::Error::Denied,"resource_not_collectible"});return {};
        }
        ayt::inventory::Result<void> consumeWorldPickup(const ayt::inventory::InteractionCandidate&)override{
            // Allocation/validation happens in a copy, so exceptions leave the
            // visible source unchanged, as required by the engine Host contract.
            auto candidate=owner_.world_;if(!candidate.collect(token_))return std::unexpected(ayt::inventory::Diagnostic{ayt::inventory::Error::Stale,"resource_action_changed"});owner_.world_=std::move(candidate);return {};
        }
    };
public:
    PluginJson commitCollection(const std::string& token,const IForagingSource::Import& import)override{
        std::lock_guard lock(mutex_);const auto pending=world_.action();if(pending.is_null()||pending.at("token")!=token||pending.at("phase")!="awaiting_inventory")return toolFailure("resource_action_changed");
        const auto item=world_.object(pending.at("id"));if(item.is_null())return toolFailure("world_object_missing");PickupHost host(*this,token);
        auto transfer=item;transfer["collection_destination"]=gather_.is_null()?"backpack":gather_.value("destination",std::string("backpack"));
        auto result=import(transfer,item.at("expires").get<double>()-world_.clock(),host);
        if(!result.value("ok",false)){world_.cancel(result.at("error").value("code",std::string("collection_failed")));finishGather(result.at("error").value("code",std::string("collection_failed")));}
        else if(!gather_.is_null()){
            gather_["results"].push_back({{"item_id",result.at("result").at("item_id")},{"source_id",item.at("id")},{"ingredient_id",supplies::resource(item)}});
            gather_["collected"]=gather_.at("collected").get<int>()+1;const auto resource=supplies::resource(item);auto& needs=gather_["needs"];if(needs.contains(resource)&&needs.at(resource).get<unsigned>()>0)needs[resource]=needs.at(resource).get<unsigned>()-1;
            gather_["phase"]="finding";
        }
        drain();invalidate();return result;
    }
private:
