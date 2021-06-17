#include <iostream>
#include "JfjFrameStats.hpp"

using namespace std;
using namespace chrono;

FrameStats::FrameStats(
        const std::string &detector_name,
        const int module_id,
        const size_t stats_time) :
            detector_name_(detector_name),
            module_id_(module_id),
            stats_time_(stats_time)
{
   reset_counters();
}

void FrameStats::reset_counters()
{
    frames_counter_ = 0;
    n_missed_packets_ = 0;
    n_corrupted_frames_ = 0;
    n_corrupted_pulse_id_ = 0;
    stats_interval_start_ = steady_clock::now();
}

void FrameStats::record_stats(const ModuleFrame &meta, const bool bad_pulse_id)
{

    if (bad_pulse_id) {
        n_corrupted_pulse_id_++;
    } 

    if (meta.n_recv_packets < JF_N_PACKETS_PER_FRAME) {
        n_missed_packets_ += JF_N_PACKETS_PER_FRAME - meta.n_recv_packets;
        n_corrupted_frames_++;
    }

    frames_counter_++;

    auto time_passed = duration_cast<milliseconds>(
             steady_clock::now()-stats_interval_start_).count(); 

    if (time_passed >= stats_time_*1000) {
        print_stats();
        reset_counters();
    }
}

void FrameStats::print_stats()
{
    auto interval_ms_duration = duration_cast<milliseconds>(
            steady_clock::now()-stats_interval_start_).count();
    // * 1000 because milliseconds, + 250 because of truncation.
    int rep_rate = ((frames_counter_ * 1000) + 250) / interval_ms_duration;
    uint64_t timestamp = time_point_cast<nanoseconds>(
            system_clock::now()).time_since_epoch().count();

    // Output in InfluxDB line protocol
    cout << "jf_udp_recv";
    cout << ",detector_name=" << detector_name_;
    cout << ",module_name=M" << module_id_;
    cout << " ";
    cout << "n_missed_packets=" << n_missed_packets_ << "i";
    cout << ",n_corrupted_frames=" << n_corrupted_frames_ << "i";
    cout << ",repetition_rate=" << rep_rate << "i";
    cout << ",n_corrupted_pulse_ids=" << n_corrupted_pulse_id_ << "i";
    cout << " ";
    cout << timestamp;
    cout << endl;
}
