#include <Hibernation/DesktopServices.h>
#include <Hibernation/ScreenGeometry.h>
#include <Hibernation/ModelEmbodiment.h>
#include <AYTestFixtures.h>
#include <Windows.h>
using namespace hibernation;
TEST_SUITE(ScreenshotRegions)
TEST_CASE(widget_geometry_is_pixel_based_and_handles_negative_screen_origins){
    const auto geometry=widgetScreenGeometry({-1920,-200,3840,1280},{-1800,-100,168,168},{-1780,-80,120,145},true);
    CHECK(geometry.at("available").get<bool>());CHECK(geometry.at("canvas").at("x")==-1800);
    CHECK(geometry.at("capture_rectangle").at("x")==-1816);CHECK(geometry.at("capture_rectangle").at("width")==200);
    CHECK(std::abs(geometry.at("capture_region").at("x").get<double>()-104.0/3840)<1e-12);
    const auto clipped=widgetScreenGeometry({-1920,0,3840,1080},{-1930,-10,100,100},{},true);
    CHECK(clipped.at("capture_rectangle").at("x")==-1920);CHECK(clipped.at("capture_region").at("x")==0);
    CHECK(!widgetScreenGeometry({0,0,1920,1080},{100,100,168,168},{},false).at("available").get<bool>());
    CHECK(!widgetScreenGeometry({0,0,1920,1080},{3000,100,168,168},{},true).at("available").get<bool>());
    CHECK(widgetBodyContext({{"screen_geometry",geometry}}).at("screen_geometry")==geometry);
}
TEST_CASE(region_capture_preserves_authority_and_validates_model_arguments){
    Config cfg;cfg.desktopEnabled=true;cfg.computer={{"enabled",true},{"allowScreenCapture",false}};
    DesktopServices service(cfg);
    auto invoke=[&](Json args){return service.invoke("computer.screenshot",args,{});};
    const Json region={{"x",0.5},{"y",0.5},{"width",0.5},{"height",0.5}};
    CHECK(invoke({{"target","screen"},{"region",region}}).at("error").at("code")=="screen_capture_not_enabled");
    CHECK(invoke({{"target","widget"}}).at("error").at("code")=="screen_capture_not_enabled");
    cfg.computer["allowScreenCapture"]=true;
    CHECK(invoke({{"target","widget"}}).at("error").at("code")=="widget_not_visible_or_unavailable");
    CHECK(invoke({{"target","widget"},{"region",region}}).at("error").at("code")=="widget_capture_region_is_program_managed");
    cfg.computer["allowScreenCapture"]=false;
    CHECK(invoke({{"target","invalid"}}).at("error").at("code")=="invalid_capture_target");
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"HibernationConversationV2";wc.lpfnWndProc=DefWindowProcW;RegisterClassW(&wc);
    auto window=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"Synthetic screenshot fixture",WS_POPUP|WS_VISIBLE,40,40,160,90,nullptr,nullptr,wc.hInstance,nullptr);CHECK(window!=nullptr);UpdateWindow(window);
    CHECK(!invoke({{"region",{{"x",0},{"width",1},{"height",1}}}}).at("ok").get<bool>());
    CHECK(!invoke({{"region",{{"x","0"},{"y",0},{"width",1},{"height",1}}}}).at("ok").get<bool>());
    CHECK(invoke({{"region",{{"x",0.8},{"y",0},{"width",0.3},{"height",1}}}}).at("error").at("code")=="invalid_capture_region");
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const auto cropped=invoke({{"target","application"},{"region",region}});
    CHECK(cropped.at("ok").get<bool>());CHECK(cropped.at("result").at("width")==80);CHECK(cropped.at("result").at("height")==45);
    CHECK(cropped.at("result").at("region")==region);CHECK(cropped.at("result").at("image").at("mime_type")=="image/jpeg");
    // Capture only our synthetic window via the same native geometry callback
    // used by the real widget, so no user desktop pixels enter the fixture.
    auto current=widgetScreenGeometry({GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN)},{40,40,160,90},{40,40,160,90},true,0);
    int geometryReads=0;DesktopServices own(cfg,[&]{++geometryReads;return current;});cfg.computer["allowScreenCapture"]=true;
    const auto ownImage=own.invoke("computer.screenshot",{{"target","widget"}},{});
    CHECK(ownImage.at("ok").get<bool>());CHECK(ownImage.at("result").at("target")=="widget");CHECK(ownImage.at("result").at("width")==160);CHECK(ownImage.at("result").at("height")==90);
    CHECK(ownImage.at("result").at("region")==current.at("capture_region"));CHECK(geometryReads==1);
    current["available"]=false;CHECK(own.invoke("computer.screenshot",{{"target","widget"}},{}).at("error").at("code")=="widget_not_visible_or_unavailable");
    if(SUCCEEDED(com))CoUninitialize();DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
TEST_SUITE_END
