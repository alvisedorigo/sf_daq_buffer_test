#include <sys/mman.h>
#include <fcntl.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <unistd.h>
#include "RamBuffer.hpp"
#include "buffer_config.hpp"

using namespace std;
using namespace buffer_config;

RamBuffer::RamBuffer(
        const string &detector_name,
        const int n_modules,
        const int n_slots) :
        detector_name_(detector_name),
        n_modules_(n_modules),
        n_slots_(n_slots),
        meta_bytes_(sizeof(ModuleFrame) * n_modules_),
        image_bytes_(MODULE_N_BYTES * n_modules_),
        buffer_bytes_((meta_bytes_ + image_bytes_) * n_slots_)
{
    shm_fd_ = shm_open(detector_name_.c_str(), O_RDWR | O_CREAT, 0777);
    if (shm_fd_ < 0) {
        throw runtime_error(strerror(errno));
    }

    if ((ftruncate(shm_fd_, buffer_bytes_)) == -1) {
        throw runtime_error(strerror(errno));
    }

    // TODO: Test with MAP_HUGETLB
    buffer_ = mmap(NULL, buffer_bytes_, PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (buffer_ == MAP_FAILED) {
        throw runtime_error(strerror(errno));
    }

    // Metadata buffer is located at the start of the memory region.
    meta_buffer_ = (ModuleFrame *) buffer_;
    // Image buffer start right after metadata buffer.
    image_buffer_ = (char*)buffer_ + (meta_bytes_ * n_slots_);
}

RamBuffer::~RamBuffer()
{
    munmap(buffer_, buffer_bytes_);
    close(shm_fd_);
    shm_unlink(detector_name_.c_str());
}

void RamBuffer::write_frame(
        const ModuleFrame& src_meta,
        const char *src_data) const
{
    const int slot_n = src_meta.pulse_id % n_slots_;

    ModuleFrame *dst_meta = meta_buffer_ +
                            (n_modules_ * slot_n) +
                            src_meta.module_id;

    char *dst_data = image_buffer_ +
                     (image_bytes_ * slot_n) +
                     (MODULE_N_BYTES * src_meta.module_id);

    memcpy(dst_meta, &src_meta, sizeof(ModuleFrame));
    memcpy(dst_data, src_data, MODULE_N_BYTES);
}

void RamBuffer::read_frame(
        const uint64_t pulse_id,
        const uint64_t module_id,
        ModuleFrame& dst_meta,
        char* dst_data) const
{
    const size_t slot_n = pulse_id % n_slots_;

    ModuleFrame *src_meta = meta_buffer_ + (n_modules_ * slot_n) + module_id;

    char *src_data = image_buffer_ +
                     (image_bytes_ * slot_n) +
                     (MODULE_N_BYTES * module_id);

    memcpy(&dst_meta, src_meta, sizeof(ModuleFrame));
    memcpy(dst_data, src_data, MODULE_N_BYTES);
}

void RamBuffer::assemble_image(
        const uint64_t pulse_id, ImageMetadata &image_meta) const
{
    const size_t slot_n = pulse_id % n_slots_;
    ModuleFrame *src_meta = meta_buffer_ + (n_modules_ * slot_n);

    auto is_pulse_init = false;
    auto is_good_image = true;

    for (int i_module=0; i_module <  n_modules_; i_module++) {
        ModuleFrame *frame_meta = src_meta + i_module;

        auto is_good_frame =
                frame_meta->n_recv_packets == JF_N_PACKETS_PER_FRAME;

        if (!is_good_frame) {
            is_good_image = false;
            continue;
        }

        if (!is_pulse_init) {

            if (frame_meta->pulse_id != pulse_id) {
                stringstream err_msg;
                err_msg << "[RamBuffer::read_image]";
                err_msg << " Unexpected pulse_id in ram buffer.";
                err_msg << " expected=" << pulse_id;
                err_msg << " got=" << frame_meta->pulse_id;

                for (int i = 0; i < n_modules_; i++) {
                    ModuleFrame *meta = src_meta + i_module;

                    err_msg << " (module " << i << ", ";
                    err_msg << meta->pulse_id << "),";
                }
                err_msg << endl;

                throw runtime_error(err_msg.str());
            }

            image_meta.id = frame_meta->pulse_id;
            image_meta.user_1 = frame_meta->frame_index;
            image_meta.user_2 = frame_meta->daq_rec;

            is_pulse_init = 1;
        }

        if (is_good_image) {
            if (frame_meta->pulse_id != image_meta.id) {
                is_good_image = false;
                // TODO: Add some diagnostics in case this happens.
            }

            if (frame_meta->frame_index != image_meta.user_1) {
                is_good_image = false;
            }

            if (frame_meta->daq_rec != image_meta.user_2) {
                is_good_image = false;
            }
        }
    }

    image_meta.status = is_good_image;

    if (!is_pulse_init) {
        image_meta.id = 0;
        image_meta.user_1 = 0;
        image_meta.user_2 = 0;
    }
}

char* RamBuffer::read_image(const uint64_t pulse_id) const
{
    const size_t slot_n = pulse_id % n_slots_;
    char *src_data = image_buffer_ + (image_bytes_ * slot_n);

    return src_data;
}


void RamBuffer::write_image(const ImageMetadata& src_meta, const char *src_data)
{
    const int slot_n = src_meta.id % n_slots_;
    const int image_n_bytes = src_meta.height * src_meta.width * TypeMap.at(src_meta.dtype).size;

    char *dst_data = image_buffer_ + (image_n_bytes * slot_n);

    // Hope this won't segfault
    memcpy(dst_data, src_data, image_n_bytes);
}



