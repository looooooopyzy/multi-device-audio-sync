#include "audio/clock_sync.h"
#include "audio/audio_packet.h"
#include "audio/acoustic_calibration.h"
#include "audio/monotonic_clock.h"
#include "audio/node_registry.h"
#include "audio/protocol.h"
#include "audio/sequence.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
using namespace audio;
void check(bool value,const char* message) { if(!value) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); } }
int main() {
  ClockSample s;
  check(ClockSample::calculate(1000000000,1020500000,1020600000,1001100000,s),"sample valid");
  check(std::abs(s.rtt_ns-1000000)<1,"RTT formula");
  check(std::abs(s.offset_ns-20000000)<1,"offset formula");
  check(ticks_to_ns(123456789,10000000)==12345678900LL,"QPC conversion");
  check(ticks_to_ns(9876543210LL,3000000)==3292181070000LL,"QPC remainder conversion");
  Packet p; p.type=MessageType::SyncResponse; p.sequence=123; p.deviceId=456;
  p.t1=123456789; p.t2=-123; p.t3=333; p.platform=Platform::Android; p.deviceName="Test";
  std::array<uint8_t,kPacketSize> bytes{};
  check(encode(p,bytes),"encode"); Packet q;
  check(decode(bytes.data(),bytes.size(),q) && q.sequence==123 && q.t2==-123 && q.deviceName=="Test","decode roundtrip");
  bytes[4]=2; check(!decode(bytes.data(),bytes.size(),q),"version rejection");
  check(!decode(bytes.data(),bytes.size()-1,q),"length rejection");
  AudioPacket audio_packet;
  audio_packet.stream_id=7; audio_packet.sequence=13; audio_packet.sample_frame=480;
  audio_packet.presentation_master_ns=1230000000;
  audio_packet.samples.assign(kAudioFramesPerPacket*kAudioChannels,0.25f);
  std::vector<uint8_t> pcm;
  check(encode_audio_packet(audio_packet,pcm),"audio packet encode");
  AudioPacket decoded_audio;
  check(decode_audio_packet(pcm.data(),pcm.size(),decoded_audio) &&
        decoded_audio.sequence==13 && decoded_audio.sample_frame==480 &&
        decoded_audio.samples[42]==0.25f,"audio packet roundtrip");
  pcm[4]=2; check(!decode_audio_packet(pcm.data(),pcm.size(),decoded_audio),"audio version rejection");
  pcm[4]=0; check(!decode_audio_packet(pcm.data(),pcm.size()-1,decoded_audio),"audio length rejection");
  SequenceTracker tracker; auto seq=tracker.next(); tracker.add(seq,{7,100,4,9000});
  check(!tracker.consume(seq,7,101,4,9000),"T1 mismatch");
  check(!tracker.consume(seq,7,100,5,9000),"endpoint mismatch");
  check(tracker.consume(seq,7,100,4,9000),"sequence consume");
  check(!tracker.consume(seq,7,100,4,9000),"duplicate rejection");
  NodeRegistry nodes; nodes.seen(1,"A","127.0.0.1",33,Platform::Simulated,100);
  nodes.expire(5000000200LL); check(nodes.find(1)->status==NodeStatus::Timeout,"timeout");
  ClockSyncEngine engine;
  std::mt19937 rng(4); std::uniform_real_distribution<double> jitter(0,5e6);
  std::uniform_real_distribution<double> loss(0,1);
  const double origin=1e12, offset=20e6, rate=1+10e-6;
  for(int i=0;i<600;++i) {
    if(loss(rng)<0.01) continue;
    double t1=origin+i*1e9, outbound=0.5e6+jitter(rng), inbound=0.5e6+jitter(rng);
    if(i%37==0) outbound+=50e6;
    int64_t a=static_cast<int64_t>(t1), d=static_cast<int64_t>(t1+outbound+inbound+100000);
    int64_t b=static_cast<int64_t>((t1+outbound)*rate+offset);
    int64_t c=static_cast<int64_t>((t1+outbound+100000)*rate+offset);
    ClockSample sample; check(ClockSample::calculate(a,b,c,d,sample),"synthetic sample");
    engine.add(sample);
  }
  auto model=engine.model();
  check(model.sample_count==256,"bounded window");
  check(model.filtered_count<model.sample_count,"outlier filtering");
  check(std::abs(model.drift_ppm-10)<3,"drift convergence");
  double true_offset=offset+10e-6*(origin+599e9);
  check(std::abs(model.offset_at(origin+599e9)-true_offset)<0.8e6,"offset convergence");
  std::vector<float> recording(4*48000,0);
  const auto probe=calibration_probe(48000);
  const auto ipad_probe=calibration_probe(48000,CalibrationProbeKind::Ipad);
  const size_t first=48000,second=first+48000+1776; // 37 ms after the nominal 1 s gap.
  std::normal_distribution<float> noise(0.0f,0.008f);
  for(float& sample:recording) sample=noise(rng);
  for(size_t i=0;i<probe.size();++i) {
    recording[first+i]+=probe[i]; recording[second+i]+=ipad_probe[i]*0.7f;
  }
  auto detected=detect_probe_pair(recording,48000);
  check(detected.valid && std::abs(detected.delta_ms-37)<1.0,"acoustic probe delay");
  std::vector<float> one_speaker(4*48000,0);
  for(size_t i=0;i<probe.size();++i) one_speaker[first+i]=probe[i];
  auto incomplete=detect_probe_pair(one_speaker,48000);
  check(!incomplete.valid && incomplete.windows_confidence>0.8 &&
        incomplete.ipad_confidence<0.27,"single speaker diagnostics");
  std::vector<float> with_tone(4*48000,0);
  for(size_t i=0;i<with_tone.size();++i)
    with_tone[i]=static_cast<float>(0.5*std::sin(2*3.141592653589793*440*i/48000.0));
  for(size_t i=0;i<probe.size();++i) {
    with_tone[first+i]+=0.17f*probe[i];
    with_tone[second+i]+=0.17f*ipad_probe[i];
  }
  auto masked=detect_probe_pair(with_tone,48000);
  check(masked.valid && std::abs(masked.delta_ms-37)<1.0,
        "acoustic probes through continuous test tone");
  std::vector<float> silence(4*48000,0);
  check(!detect_probe_pair(silence,48000).valid,"acoustic probe rejects silence");
  std::cout<<"shared tests passed: offset error "<<
    (model.offset_at(origin+599e9)-true_offset)/1e6<<" ms, drift "<<model.drift_ppm<<" ppm\n";
}
