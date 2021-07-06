#include <H5version.h>
#include <iostream>

#include "JFH5Writer.hpp"
#include "live_writer_config.hpp"
#include "buffer_config.hpp"
#include "formats.hpp"

extern "C"
{
    #include <bitshuffle/bshuf_h5filter.h>
}

using namespace std;
using namespace buffer_config;
using namespace live_writer_config;

JFH5Writer::JFH5Writer(const BufferUtils::DetectorConfig& config):
        root_folder_(config.buffer_folder),
        detector_name_(config.detector_name)
{
}

JFH5Writer::~JFH5Writer()
{
    close_file();
}

hid_t JFH5Writer::get_datatype(const int bits_per_pixel)
{
    switch(bits_per_pixel) {
        case 8:
            return H5T_NATIVE_UINT8;
        case 16:
            return H5T_NATIVE_UINT16;
        case 32:
            return H5T_NATIVE_UINT32;
        default:
            throw runtime_error(
                    "Unsupported bits per pixel:" + to_string(bits_per_pixel));
    }
}

void JFH5Writer::open_run(const int64_t run_id,
                          const uint32_t n_images,
                          const uint32_t image_y_size,
                          const uint32_t image_x_size,
                          const uint32_t bits_per_pixel)
{
    close_run();

    const string output_folder = root_folder_ + "/" + OUTPUT_FOLDER_SYMLINK;
    // TODO: Maybe add leading zeros to filename?
    const string output_file = output_folder + to_string(run_id) + ".h5";

    current_run_id_ = run_id;
    image_y_size_ = image_y_size;
    image_x_size_ = image_x_size;
    bits_per_pixel_ = bits_per_pixel;
    image_n_bytes_ = (image_y_size_ * image_x_size_ * bits_per_pixel_) / 8;

#ifdef DEBUG_OUTPUT
    cout << "[JFH5Writer::open_run]";
    cout << " run_id:" << current_run_id_;
    cout << " output_file:" << output_file;
    cout << " bits_per_pixel:" << bits_per_pixel_;
    cout << " image_y_size:" << image_y_size_;
    cout << " image_x_size:" << image_x_size_;
    cout << " image_n_bytes:" << image_n_bytes_;
    cout << endl;
#endif

    open_file(output_file, n_images);
}

void JFH5Writer::close_run()
{

#ifdef DEBUG_OUTPUT
    cout << "[JFH5Writer::close_run] run_id:" << current_run_id_ << endl;
#endif

    close_file();

    current_run_id_ = NO_RUN_ID;
    image_y_size_ = 0;
    image_x_size_ = 0;
    bits_per_pixel_ = 0;
    image_n_bytes_ = 0;
}

void JFH5Writer::open_file(const string& output_file, const uint32_t n_images)
{
    // Create file
    auto fcpl_id = H5Pcreate(H5P_FILE_ACCESS);
    if (fcpl_id == -1) {
        throw runtime_error("Error in file access property list.");
    }

    if (H5Pset_fapl_mpio(fcpl_id, MPI_COMM_WORLD, MPI_INFO_NULL) < 0) {
        throw runtime_error("Cannot set mpio to property list.");
    }

    file_id_ = H5Fcreate(
            output_file.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, fcpl_id);
    if (file_id_ < 0) {
        throw runtime_error("Cannot create output file.");
    }

    H5Pclose(fcpl_id);

    // Create group
    auto data_group_id = H5Gcreate(file_id_, detector_name_.c_str(),
                                   H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (data_group_id < 0) {
        throw runtime_error("Cannot create data group.");
    }

    // Create image dataset.
    auto dcpl_id = H5Pcreate(H5P_DATASET_CREATE);
    if (dcpl_id < 0) {
        throw runtime_error("Error in creating dataset create property list.");
    }

    hsize_t image_dataset_chunking[] = {1, image_y_size_, image_x_size_};
    if (H5Pset_chunk(dcpl_id, 3, image_dataset_chunking) < 0) {
        throw runtime_error("Cannot set image dataset chunking.");
    }

    if (H5Pset_fill_time(dcpl_id, H5D_FILL_TIME_NEVER) < 0) {
        throw runtime_error("Cannot set image dataset fill time.");
    }

    if (H5Pset_alloc_time(dcpl_id, H5D_ALLOC_TIME_EARLY) < 0) {
        throw runtime_error("Cannot set image dataset allocation time.");
    }

    hsize_t image_dataset_dims[] = {n_images, image_y_size_, image_x_size_};
    auto image_space_id = H5Screate_simple(3, image_dataset_dims, nullptr);
    if (image_space_id < 0) {
        throw runtime_error("Cannot create image dataset space.");
    }

    // TODO: Enable compression.
//    bshuf_register_h5filter();
//    uint filter_prop[] = {0, BSHUF_H5_COMPRESS_LZ4};
//    if (H5Pset_filter(dcpl_id, BSHUF_H5FILTER, H5Z_FLAG_MANDATORY,
//                      2, filter_prop) < 0) {
//        throw runtime_error("Cannot set compression filter on dataset.");
//    }

    image_dataset_id_ = H5Dcreate(
            data_group_id, "data", get_datatype(bits_per_pixel_),
            image_space_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT);
    if (image_dataset_id_ < 0) {
        throw runtime_error("Cannot create image dataset.");
    }

    // Create metadata datasets.
    hsize_t meta_dataset_dims[] = {n_images};
    auto meta_space_id = H5Screate_simple(1, meta_dataset_dims, nullptr);
    if (meta_space_id < 0) {
        throw runtime_error("Cannot create meta dataset space.");
    }

    auto create_meta_dataset = [&](const string& name, hid_t data_type) {
        auto dataset_id = H5Dcreate(
                data_group_id, name.c_str(), data_type, meta_space_id,
                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (dataset_id < 0) {
            throw runtime_error("Cannot create " + name + " dataset.");
        }

        return dataset_id;
    };

    pulse_dataset_id_ = create_meta_dataset("pulse_id", H5T_NATIVE_UINT64);
    frame_dataset_id_ = create_meta_dataset("frame_index", H5T_NATIVE_UINT64);
    daq_rec_dataset_id_ = create_meta_dataset("daq_rec", H5T_NATIVE_UINT32);
    is_good_dataset_id_ = create_meta_dataset("is_good_frame", H5T_NATIVE_UINT8);

    H5Sclose(meta_space_id);
    H5Sclose(image_space_id);
    H5Pclose(dcpl_id);
    H5Gclose(data_group_id);
}

void JFH5Writer::close_file()
{
    if (file_id_ < 0) {
        return;
    }

    H5Dclose(image_dataset_id_);
    image_dataset_id_ = -1;

    H5Dclose(pulse_dataset_id_);
    pulse_dataset_id_ = -1;

    H5Dclose(frame_dataset_id_);
    frame_dataset_id_ = -1;

    H5Dclose(daq_rec_dataset_id_);
    daq_rec_dataset_id_ = -1;

    H5Dclose(is_good_dataset_id_);
    is_good_dataset_id_ = -1;

    H5Fclose(file_id_);
    file_id_ = -1;
}

void JFH5Writer::write_data(
        const int64_t run_id, const uint32_t index, const char* data)
{
    if (run_id != current_run_id_) {
        throw runtime_error("Invalid run_id.");
    }

    const hsize_t ram_dims[3] = {1, image_y_size_, image_x_size_};
    auto ram_ds = H5Screate_simple(3, ram_dims, nullptr);
    if (ram_ds < 0) {
        throw runtime_error("Cannot create image ram dataspace.");
    }

    auto file_ds = H5Dget_space(image_dataset_id_);
    if (file_ds < 0) {
        throw runtime_error("Cannot get image dataset file dataspace.");
    }

    const hsize_t file_ds_start[] = {index, 0, 0};
    const hsize_t file_ds_stride[] = {1, 1, 1};
    const hsize_t file_ds_count[] = {1, image_y_size_, image_x_size_};
    const hsize_t file_ds_block[] = {1, 1, 1};
    if (H5Sselect_hyperslab(file_ds, H5S_SELECT_SET,
            file_ds_start, file_ds_stride, file_ds_count, file_ds_block) < 0) {
       throw runtime_error("Cannot select image dataset file hyperslab.");
    }

    if (H5Dwrite(image_dataset_id_, get_datatype(bits_per_pixel_),
            ram_ds, file_ds, H5P_DEFAULT, data) < 0) {
        throw runtime_error("Cannot write data to image dataset.");
    }

    H5Sclose(file_ds);
    H5Sclose(ram_ds);
}

void JFH5Writer::write_meta(
        const int64_t run_id, const uint32_t index, const ImageMetadata& meta)
{
    if (run_id != current_run_id_) {
        throw runtime_error("Invalid run_id.");
    }

    const hsize_t ram_dims[3] = {1, 1, 1};
    auto ram_ds = H5Screate_simple(3, ram_dims, nullptr);
    if (ram_ds < 0) {
        throw runtime_error("Cannot create metadata ram dataspace.");
    }

    auto file_ds = H5Dget_space(pulse_dataset_id_);
    if (file_ds < 0) {
        throw runtime_error("Cannot get metadata dataset file dataspace.");
    }

    const hsize_t file_ds_start[] = {index, 0, 0};
    const hsize_t file_ds_stride[] = {1, 1, 1};
    const hsize_t file_ds_count[] = {1, 1, 1};
    const hsize_t file_ds_block[] = {1, 1, 1};
    if (H5Sselect_hyperslab(file_ds, H5S_SELECT_SET,
            file_ds_start, file_ds_stride, file_ds_count, file_ds_block) < 0) {
        throw runtime_error("Cannot select metadata dataset file hyperslab.");
    }

    if (H5Dwrite(pulse_dataset_id_, H5T_NATIVE_UINT64,
            ram_ds, file_ds, H5P_DEFAULT, &(meta.pulse_id)) < 0) {
        throw runtime_error("Cannot write data to pulse_id dataset.");
    }

    if (H5Dwrite(frame_dataset_id_, H5T_NATIVE_UINT64,
                 ram_ds, file_ds, H5P_DEFAULT, &(meta.frame_index)) < 0) {
        throw runtime_error("Cannot write data to frame_index dataset.");
    }

    if (H5Dwrite(daq_rec_dataset_id_, H5T_NATIVE_UINT32,
                 ram_ds, file_ds, H5P_DEFAULT, &(meta.daq_rec)) < 0) {
        throw runtime_error("Cannot write data to daq_rec dataset.");
    }

    if (H5Dwrite(is_good_dataset_id_, H5T_NATIVE_UINT32,
                 ram_ds, file_ds, H5P_DEFAULT, &(meta.is_good_image)) < 0) {
        throw runtime_error("Cannot write data to is_good_image dataset.");
    }

    H5Sclose(file_ds);
    H5Sclose(ram_ds);
}
