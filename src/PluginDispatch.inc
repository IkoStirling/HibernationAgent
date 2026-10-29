    Json lastSuppliesStatus=nullptr;
    std::optional<bool> lastCanteenAvailability;
    void processPluginEvents(std::int64_t now){const bool canteenEnabled=plugins->enabled("canteen");if(!lastCanteenAvailability||*lastCanteenAvailability!=canteenEnabled){lastCanteenAvailability=canteenEnabled;plugins->event({{"kind","runtime.canteen.available"},{"enabled",canteenEnabled}});}const auto kitchen=plugins->liveSnapshots().value("canteen",Json::object()).value("current",Json::object());const Json supplyStatus={{"kind","runtime.supplies.status"},{"available",canteenEnabled&&!kitchen.contains("issue")},{"menu",kitchen.value("menu",Json::array())},{"pantry",kitchen.value("pantry",Json::array())}};if(supplyStatus!=lastSuppliesStatus){lastSuppliesStatus=supplyStatus;plugins->event(supplyStatus);}
        for(auto event:plugins->takeEvents()){
            if(event.value("plugin",std::string{})=="habitat"&&event.value("kind",std::string{})=="habitat.collection.request"){plugins->commitCollection(event.value("token",std::string{}));continue;}
            if(event.value("plugin",std::string{})=="canteen"&&event.value("kind",std::string{})=="canteen.inventory_consumed")plugins->event({{"kind","runtime.supplies.consumed"},{"nutrition",event.value("nutrition",0.0)}});
            if(canteenEnabled&&event.value("plugin",std::string{})=="canteen"&&event.value("kind",std::string{})=="canteen.delivery.request"){const auto pending=plugins->liveSnapshots().value("canteen",Json::object()).value("outbox",Json(nullptr));if(pending.is_object()&&pending.at("ticket")==event.value("ticket",std::string{}))plugins->event({{"kind","runtime.canteen.deliver"},{"request",pending}});continue;}
            if(event.value("plugin",std::string{})=="habitat"&&event.value("kind",std::string{})=="habitat.canteen_delivery"){auto receipt=event;receipt["kind"]="runtime.canteen.delivery_result";plugins->event(receipt);continue;}

            if(event.value("plugin",std::string{})=="napcat"&&event.value("kind",std::string{})=="napcat.model.usage"){recordModelUsage(event.value("profile_id",std::string{}),"qq_"+event.value("purpose",std::string{}),event.value("usage",Json::object()));continue;}
            if(napcatControl(event))continue;
            if(event.value("plugin",std::string{})=="speech-style"&&event.value("kind",std::string{})=="style.changed"){
                reactionGenerator.cancel();saveRuntime();continue; // Configuration is not a character experience.
            }
            if(event.value("kind","")=="ui.chat.open"){
                if(cfg.desktopEnabled&&event.value("plugin","")=="widget"&&event.contains("x")&&event.contains("y")&&event.at("x").is_number_integer()&&event.at("y").is_number_integer()&&std::abs(event.at("x").get<double>())<=100000&&std::abs(event.at("y").get<double>())<=100000){desktopRequests=Json::array({{{"x",event.at("x")},{"y",event.at("y")},{"mode",event.value("mode",std::string("floating"))}}});}
                continue;
            }
            if(event.value("kind","")=="speech.prepared"){reactionPrepared(event);continue;}
            activityEvent(event);
            trackSpeech(event);
            if(event.value("kind","")=="speech.failed")speechIssue="语音未能播放，请在管理页检查声音设置。";
            if(event.value("kind","")=="speech.started")speechIssue.clear();
            if(event.value("kind","")=="weather.observed")life.weather(event.at("temperature"),event.at("weather_code"),event.value("observed_ms",now));
            plugins->event(event);
            const auto transientKind=event.value("kind",std::string{});
            if(transientKind=="speech.progress"||transientKind=="widget.position"||transientKind=="rhythm.pose.request"||transientKind=="rhythm.pose.release"||transientKind=="widget.rest_started"||transientKind=="widget.rest_interrupted")continue; // Pose acknowledgements, playback and position telemetry are ephemeral.
            const auto source=recordPluginEvent(event,now);if(event.value("plugin",std::string{})=="weather"&&event.value("kind",std::string{})=="weather.observed")rememberedWeather(event,source);if(event.value("plugin",std::string{})=="television"&&event.value("kind",std::string{})=="television.ended")rememberedSignal("video_finished",source,event.value("title",std::string{}));if(event.value("plugin",std::string{})=="habitat"&&event.value("kind",std::string{})=="habitat.consumed")rememberedSignal("item_consumed",source,event.value("item",Json::object()).value("name",std::string{}));if(event.value("kind","")=="widget.interacted")reactToDrag(event,source,now);
        }}
