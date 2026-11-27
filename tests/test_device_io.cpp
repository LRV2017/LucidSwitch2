#include "lucid/devices/audio_mixer.hpp"
#include "lucid/devices/controller_hub.hpp"
#include "lucid/devices/sdl_host_io.hpp"

#include <array>
#include <cstdint>
#include <iostream>

namespace {
int failures=0;
void expect(bool condition,const char* message){
    if(condition)std::cout<<"[PASS] "<<message<<"\n";
    else{++failures;std::cerr<<"[FAIL] "<<message<<"\n";}
}
void test_controller_hub(){
    using namespace lucid::devices;
    ControllerHub hub;
    expect(!hub.connect(ControllerHub::MaxControllers),"reject an invalid controller slot");
    expect(hub.connect(0U),"connect a controller slot");
    expect(!hub.connect(0U),"reject duplicate controller connection");
    ControllerSnapshot state{};
    state.buttons=button_bit(ControllerButton::A)|button_bit(ControllerButton::ZL);
    state.left_x=-12345;state.left_y=23456;state.right_x=32767;state.right_y=-32768;
    state.left_trigger=12000U;state.right_trigger=32000U;
    expect(hub.publish(0U,state),"publish a controller input snapshot");
    ControllerSnapshot read{};
    expect(hub.read(0U,read),"read the current controller snapshot");
    expect(read.connected&&read.buttons==state.buttons&&read.left_x==-12345&&
           read.right_y==-32768&&read.left_trigger==12000U&&read.sequence>=2U,
           "preserve buttons, axes, triggers and monotonic update sequence");
    auto invalid=state;invalid.buttons=0x80000000U;
    expect(!hub.publish(0U,invalid),"reject undefined controller button bits");
    invalid=state;invalid.right_trigger=40000U;
    expect(!hub.publish(0U,invalid),"reject trigger values outside normalized range");
    expect(hub.disconnect(0U),"disconnect controller and clear its state");
    expect(!hub.read(0U,read),"do not expose stale input after disconnect");
}
void test_audio_mixer(){
    using namespace lucid::devices;
    AudioMixer mixer;
    const std::array<std::int16_t,4U> first{10000,-10000,30000,30000};
    const std::array<std::int16_t,4U> second{-4000,4000,30000,30000};
    expect(mixer.submit_stereo(0U,first),"queue stereo PCM on channel zero");
    expect(mixer.submit_stereo(1U,second),"queue stereo PCM on channel one");
    std::array<std::int16_t,4U> output{};
    expect(mixer.mix_stereo(output)==2U,"mix two stereo PCM frames");
    expect(output[0]==6000&&output[1]==-6000&&output[2]==32767&&output[3]==32767,
           "sum channels and saturate to signed 16-bit output");
    expect(mixer.queued_frames(0U)==0U&&mixer.queued_frames(1U)==0U,
           "consume queued frames from all active channels");
    expect(mixer.set_gain(0U,16384U),"set half-scale Q15 channel gain");
    const std::array<std::int16_t,2U> quiet{20000,-20000};
    expect(mixer.submit_stereo(0U,quiet),"submit PCM after changing gain");
    std::array<std::int16_t,2U> one_frame{};
    expect(mixer.mix_stereo(one_frame)==1U&&one_frame[0]==10000&&one_frame[1]==-10000,
           "apply deterministic Q15 channel gain");
    const std::array<std::int16_t,1U> malformed{1};
    expect(!mixer.submit_stereo(0U,malformed),"reject malformed odd-length stereo samples");
    expect(mixer.set_gain(0U,AudioMixer::UnityGainQ15),"restore unity channel gain");
    expect(!mixer.set_gain(0U,40000U),"reject gain greater than unity");
    const auto stats=mixer.stats();
    expect(stats.submitted_frames==5U&&stats.mixed_frames==3U&&
           stats.rejected_submissions==0U,
           "report submitted and mixed PCM frame counts");
}
}
void test_optional_sdl_host_adapter() {
    using namespace lucid::devices;
    ControllerHub controllers;
    AudioMixer mixer;
    SdlHostIo host_io(controllers,mixer);
    std::string error;
    if(!SdlHostIo::compiled_with_sdl2()){
        expect(!host_io.initialize(&error)&&!error.empty(),
            "report a clear unavailable result when SDL2 is not compiled in");
        expect(!host_io.open_audio_device(&error)&&!host_io.audio_device_open(),
            "reject audio-device startup in a no-SDL build");
        expect(host_io.poll_controllers()==0U,
            "return no host controllers in a no-SDL build");
    }else{
        expect(true,"SDL2 host audio/controller adapter is compiled into this build");
        expect(!host_io.audio_device_open(),
            "keep audio device closed until explicitly requested");
    }
    host_io.shutdown();
}
int main(){test_controller_hub();test_audio_mixer();test_optional_sdl_host_adapter();if(failures){std::cerr<<failures<<" device I/O test(s) failed.\n";return 1;}
std::cout<<"All device I/O tests passed.\n";return 0;}
