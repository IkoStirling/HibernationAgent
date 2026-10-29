    void processPluginEvents(std::int64_t now){for(auto event:plugins->takeEvents()){
            if(event.value("plugin",std::string{})=="napcat"&&event.value("kind",std::string{})=="napcat.model.usage"){recordModelUsage(event.value("profile_id",std::string{}),"qq_"+event.value("purpose",std::string{}),event.value("usage",Json::object()));continue;}
            if(napcatControl(event))continue;
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
