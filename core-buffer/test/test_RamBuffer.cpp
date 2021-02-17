#include "gtest/gtest.h"
#include "RamBuffer.hpp"

using namespace std;
using namespace buffer_config;

TEST(RamBuffer, simple_store)
{
    const int n_modules = 3;
    RamBuffer buffer("test_detector", n_modules, 10);

    ModuleFrame frame_meta;
    frame_meta.pulse_id = 123523;
    frame_meta.daq_rec = 1234;
    frame_meta.frame_index = 12342300;
    frame_meta.n_recv_packets = EIGER_N_PACKETS_PER_FRAME;

    auto frame_buffer = make_unique<uint16_t[]>(MODULE_N_PIXELS);


    for (size_t i = 0; i < MODULE_N_PIXELS; i++) {
        frame_buffer[i] = i % 100;
    }

    for (int i_module=0; i_module<n_modules; i_module++) {
        frame_meta.module_id = i_module;

        buffer.write_frame(frame_meta, (char *) (frame_buffer.get()));
    }

    ImageMetadata image_meta;
    buffer.assemble_image(frame_meta.pulse_id, image_meta);
    ASSERT_EQ(image_meta.pulse_id, frame_meta.pulse_id);
    ASSERT_EQ(image_meta.daq_rec, frame_meta.daq_rec);
    ASSERT_EQ(image_meta.frame_index, frame_meta.frame_index);
    ASSERT_EQ(image_meta.is_good_image, 1);
}
