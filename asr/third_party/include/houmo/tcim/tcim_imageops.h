/*
 * Copyright (c) 2022 The Houmo.ai Authors. All rights reserved.
 */

#ifndef __TCIM_IMAGE_PROCESS_H__
#define __TCIM_IMAGE_PROCESS_H__
#include <tcim/tcim_runtime.h>

#include <array>

namespace tcim {
namespace ImageOps {

/*! \class Config
 *  \brief Manages global configuration settings for image processing operations.
 */
class TCIM_API Config {
 public:
  /*!
   *  \brief Retrieves the global configuration object.
   *  \return A reference to the global Config object.
   */
  static Config& Global();

  /*!
   * \brief Sets the directory path for image processing configuration files.
   *
   * TCIM provides configurations files for image processing. By default, these
   * files are located in:
   *
   * - ``/usr/local/houmo/data`` within the software platform in the provided Docker image.
   * - ``${PATH}/houmo-tcim-runtime_<release>_${distro}_$arch/houmo-tcim-runtime/data``
   *   within the TCIM runtime software package.
   *
   * \note The directory path to the configuration files must be set before
   *       processing images.
   *
   *  \param[in] path The directory path where the configuration files are stored.
   * \return A reference to the Config object.
   */
  Config& SetDirPath(const std::string& path);

  /*!
   * \brief Sets the environment variable name used to specify the directory path
   *        for image processing configuration files.
   * \param[in] env_name The environment variable name.
   * \return A reference to the Config object.
   */
  Config& SetEnvName(const std::string& env_name);

  /*!
   *  \brief Retrieves the directory path configured for the image processing
   *         configuration files.
   *  \return The directory path as a string.
   */
  std::string GetDirPath() const;

 private:
  Config() = default;                           // User should use Global() to get the global instance
  std::string env_name_ = "TCIM_RUNTIME_PATH";  // Default environment variable name
  std::string dir_path_ = "";                   // Resource directory path
};

/*! \struct RectPad
 * \brief Struct describing the padding configurations.
 */
struct RectPad {
  int32_t left;   /*!< The padding to be applied on the left side. */
  int32_t top;    /*!< The padding to be applied on the top side. */
  int32_t right;  /*!< The padding to be applied on the right side. */
  int32_t bottom; /*!< The padding to be applied on the bottom side. */

  /*!
   * \brief Constructor to initialize the padding values.
   * \param[in] li The left padding value.
   * \param[in] ti The top padding value.
   * \param[in] ri The right padding value.
   * \param[in] bi The bottom padding value.
   */
  RectPad(int64_t li, int64_t ti, int64_t ri, int64_t bi) : left(li), top(ti), right(ri), bottom(bi) {}
};

/*! \struct RectRoi
 * \brief Struct describing the Region of Interest (ROI) configurations.
 */
struct RectRoi {
  int32_t x; /*!< The x-coordinate of the top-left corner of the ROI. */
  int32_t y; /*!< The y-coordinate of the top-left corner of the ROI. */
  int32_t w; /*!< The width of the ROI. */
  int32_t h; /*!< The height of the ROI. */

  /*!
   * \brief Constructs a RectRoi with the specified top-left corner coordinates
   *        and dimensions.
   * \param[in] xi The x-coordinate of the top-left corner of the ROI.
   * \param[in] yi The y-coordinate of the top-left corner of the ROI.
   * \param[in] wi The width of the ROI.
   * \param[in] hi The height of the ROI.
   */
  RectRoi(int64_t xi, int64_t yi, int64_t wi, int64_t hi) : x(xi), y(yi), w(wi), h(hi) {}
};

/*! \class ImageRunner
 *  \brief A class to provide base APIs for image processing operations.
 */
class ImageRunner {
 public:
  /*! \brief Sets the stream used for image processing operations.
   *  \param[in] stream A reference to the stream used for image processing operations.
   *  \return Returns the status of the function call.
   */
  virtual Status SetStream(tcim::Stream& stream) = 0;

  /*! \brief Retrieves the stream used for image processing operations.
   *  \return A reference to the stream object.
   */
  virtual tcim::Stream& GetStream() = 0;

  /*!
   * \brief Synchronizes the stream, ensuring that all tasks in the stream are
   *        completed.
   *
   * \return Returns the status of the function call.
   */
  virtual Status Sync() = 0;

  /*! \brief Retrieves the total number of input tensors.
   *  \return Returns the total number of input tensors.
   */
  virtual size_t GetInputNum() = 0;

  /*! \brief Retrieves the total number of output tensors.
   *  \return Returns the total number of output tensors.
   */
  virtual size_t GetOutputNum() = 0;

  /*! \brief Retrieves input tensor with given index.
   *  \param[in] indx The index of the input tensor.
   *  \return A reference to the input tensor at the specified index.
   */
  virtual tcim::Tensor& GetInputTensor(size_t indx) = 0;

  /*! \brief Retrieves output tensor with given index.
   *  \param[in] indx The index of the output tensor.
   *  \return A reference to the output tensor at the specified index.
   */
  virtual tcim::Tensor& GetOutputTensor(size_t indx) = 0;
};

/*! \class Resizer
 *  \brief A class for image processing operations including cropping,
 *         resizing, padding, and implementation with multiple
 *         interpolation modes.
 */
class TCIM_API Resizer : public ImageRunner {
 public:
  /*! \struct Option
   *  \brief Struct describing the configurations for initializing
   *         a Resizer object.
   */
  struct Option {
    /*! \brief The input data format.*/
    tcim::DataFmt format;
    /*! \brief The maximum width of output.*/
    int64_t max_w = 1920;
    /*! \brief The maximum height of output.*/
    int64_t max_h = 1080;
    /*! \brief The logical ID of the Houmo device used for image processing. */
    int64_t device_id;

    /*! \brief Constructs an Option object.
     *
     *  \param[in] fmt The input format of the YUV image, defined in ::DataFmt.
     *  \param[in] dev_id The logical ID of the Houmo device used for
     *   image processing. The device 0 is used by default. You can
     *   retrieve the logical device IDs via SMI tool.
     *   See "SMI Tool User Guide" for details.
     */
    explicit Option(tcim::DataFmt fmt = tcim::DataFmt::YUV422SP, int dev_id = 0) : format(fmt), device_id(dev_id) {}

    /*! \brief Sets the maximum width and height of the output image.
     *
     *  \param[in] w Maximum width of the output image.
     *  \param[in] h Maximum height of the output image.
     *  \return Reference to the current Option object.
     */
    Option& SetMaxSize(int64_t w, int64_t h) {
      max_w = w, max_h = h;
      return *this;
    }
  };

  /*!
   * \brief Interpolation modes used in image processing.
   */
  enum EnInterpMode : int32_t {
    Nearest = 0, /*! \brief Nearest neighbor interpolation. */
    Bilinear = 1 /*! \brief Bilinear interpolation. */
  };

  /*!
   * \brief The alignment modes for scaling operations.
   */
  enum EnAlignMode : int32_t {
    AlignCornerFalse = 0, /*! \brief The image corners are not aligned during scaling. */
    AlignCornerTrue = 1   /*! \brief The image corners are aligned during scaling. */
  };

  /*! \struct RunOption
   *  \brief Struct describing the runtime options for image processing
   *         operations including resizing, cropping, padding, and
   *         interpolation.
   */
  struct RunOption {
    /*! \brief The region to crop from the original image before image resizing. */
    RectRoi crop;
    /*! \brief The padding to apply to the image after resizing. */
    RectPad pad;
    /*! \brief The alignment mode to apply during resizing. */
    EnInterpMode interp_mode;
    /*! \brief The alignment mode to apply during resizing. */
    EnAlignMode align_mode;
    /*! \brief The padding values for each channel. */
    std::array<int32_t, 3> pad_val = {0, 128, 128};

    RunOption() = delete;

    /*!
     * \brief Constructor to initialize runtime options for image processing operations.
     *
     * This constructor allows users to specify the crop region, padding configuration,
     * interpolation method, and alignment mode for the resizing operation.
     *
     * \param[in] roi The region of interest (ROI) to crop, defined in RectRoi.
     * \param[in] pad The padding configuration, defined in RectPad.
     *            By default, no padding is applied.
     * \param[in] interp_mod The interpolation method, defined in Resizer::EnInterpMode.
     *            The default is ``Bilinear``.
     * \param[in] align_mod The alignment mode, defined in Resizer::EnAlignMode.
     *            The default is ``AlignCornerFalse``.
     */
    explicit RunOption(const RectRoi& roi, const RectPad& pad = {0, 0, 0, 0},
                       EnInterpMode interp_mod = EnInterpMode::Bilinear,
                       EnAlignMode align_mod = EnAlignMode::AlignCornerFalse)
        : crop(roi), pad(pad), interp_mode(interp_mod), align_mode(align_mod) {}

    /*!
     * \brief Sets the region of interest (ROI) for cropping.
     *
     * This function allows the user to specify a region of interest (ROI) that defines the
     * area to crop from the image before the resizing operation is applied.
     *
     * \param[in] roi The region of interest (ROI) for cropping, defined in RectRoi.
     * \return A reference to the current RunOption object.
     */
    RunOption& SetCrop(RectRoi& roi) {
      crop = roi;
      return *this;
    }

    /*!
     * \brief Sets the configuration for padding.
     *
     * \param[in] padsz The padding configuration, defined in RectPad.
     * \return A reference to the current RunOption object.
     */
    RunOption& SetPad(RectPad& padsz) {
      pad = padsz;
      return *this;
    }

    /*!
     * \brief Sets the padding values for each channel.
     *
     * This function sets the padding values for each channel, which are
     * applied to the image after resizing.
     *
     * \param[in] val An array containing the padding values
     *            for each channel.
     * \return A reference to the current RunOption object.
     */
    RunOption& SetPadVal(const std::array<int32_t, 3>& val) {
      pad_val = val;
      return *this;
    }
  };

  /*! \brief Default constructor.
   */
  Resizer();

  /*! \brief Resizer destructor.
   */
  virtual ~Resizer();

  /*! \brief Initializes the Resizer object with the provided configuration.
   *
   *  \note You must call this function before processing images.
   *
   *  \param[in] option Configuration for the image processing.
   *  \return Returns the status of the constructor function.
   */
  Status Init(const Option& option = Option());

  /**
   * \brief Processing images with the given configurations.
   * \param[in] input The input tensor containing the image in YUV format.
   *            This tensor can be stored either on host or Houmo device memory.
   * \param[out] output The output tensor where the processed image will
   *             be stored. This tensor can be stored either on host or Houmo device
   *             memory.
   * \param[in] sync A flag to indicate if to automatically synchronize the stream.
   *            If set to ``true``, this function will wait for all tasks related to
   *            image processing in the stream to complete before returning. If set
   *            to ``false``, this function will be executed asynchronously, returning
   *            immediately without waiting.
   * \return Returns the status of the constructor function.
   */
  Status Run(const tcim::Tensor& input, tcim::Tensor& output, const RunOption& run_option, bool sync = false);

  // Overridden interface methods
  /*! \brief Sets the stream used for image processing.
   *  \param[in] stream A reference to the stream used for image processing.
   *  \return Returns the status of the function call.
   */
  virtual Status SetStream(tcim::Stream& stream) override;

  /*! \brief Retrieves the stream used for image processing.
   *  \return A reference to the stream object.
   */
  virtual tcim::Stream& GetStream() override;

  /*!
   * \brief Synchronizes the stream, ensuring that all tasks in the stream are
   *        completed.
   *
   * \return Returns the status of the function call.
   */
  virtual Status Sync() override;

  /*! \brief Retrieves the total number of input tensors.
   *  \return Returns the total number of input tensors.
   */
  virtual size_t GetInputNum() override;

  /*! \brief Retrieves the total number of output tensors.
   *  \return Returns the total number of output tensors.
   */
  virtual size_t GetOutputNum() override;

  /*! \brief Retrieves input tensor with the given index.
   *  \param[in] indx The index of the input tensor.
   *  \return A reference to the input tensor at the specified index.
   */
  virtual tcim::Tensor& GetInputTensor(size_t indx) override;

  /*! \brief Retrieves output tensor with the given index.
   *  \param[in] indx The index of the output tensor.
   *  \return A reference to the output tensor at the specified index.
   */
  virtual tcim::Tensor& GetOutputTensor(size_t indx) override;

 private:
  class ResizerImpl;
  std::shared_ptr<ResizerImpl> impl_;  // PIMPL implementation
};

/*! \class ColorConverter
 *  \brief A class for converting images from YUV420SP, YUV422SP, or YUV444SP format
 *         to RGB Planar format.
 */
class TCIM_API ColorConverter : public ImageRunner {
 public:
  /*! \struct Option
   *  \brief Struct describing the configurations for initializing a ColorConverter object.
   */
  struct Option {
    /*! \brief Constructs an Option object.
     *
     *  \param[in] fmt The format of the YUV image, defined in ::DataFmt.
     *  \param[in] dev_id The logical ID of the Houmo device used for
     *   image format conversion. The device 0 is used by default. You can
     *   retrieve the logical device IDs via SMI tool.
     *   See "SMI Tool User Guide" for details.
     */
    Option(tcim::DataFmt fmt = tcim::DataFmt::YUV422SP, int dev_id = 0) : fmt(fmt), device_id(dev_id) {}

    /*! \brief Sets the maximum width and height of the input image.
     *
     *  \param[in] w Maximum width of the input image.
     *  \param[in] h Maximum height of the input image.
     *  \return Reference to the current Option object.
     */
    Option& SetMaxSize(int64_t w, int64_t h) {
      max_w = w;
      max_h = h;
      return *this;
    }

    /*! \brief The input image format. */
    tcim::DataFmt fmt;
    /*! \brief The logical ID of the Houmo device used for image format conversion.*/
    int64_t device_id = 0;
    /*! \brief The maximum width of the input image.*/
    int64_t max_w = 1920;
    /*! \brief The maximum height of the input image.*/
    int64_t max_h = 1080;
  };

  /*! \brief Default constructor.
   */
  ColorConverter();

  /*! \brief ColorConverter destructor.
   */
  virtual ~ColorConverter();

  /*! \brief Initializes the ColorConverter object with the provided configuration.
   *
   *  \note You must call this function before image format conversion.
   *
   *  \param[in] option Configuration for the image format conversion.
   *  \return Returns the status of the constructor function.
   */
  virtual Status Init(const Option& option = Option());

  /**
   * \brief Converts images from YUV420SP, YUV422SP, or YUV444SP format
   *        to RGB Planar format.
   * \param[in] input The input tensor containing the image in YUV format.
   *            This tensor can be stored either on host or Houmo device memory.
   * \param[out] output The output tensor where the converted RGB Planar image will
   *             be stored. This tensor can be stored either on host or Houmo device
   *             memory.
   * \param[in] sync A flag to indicate if to automatically synchronize the stream.
   *            If set to ``true``, this function will wait for all tasks related to
   *            format conversion in the stream to complete before returning. If set
   *            to ``false``, this function will be executed asynchronously, returning
   *            immediately without waiting.
   * \return Returns the status of the constructor function.
   */
  Status Run(const tcim::Tensor& input, tcim::Tensor& output, bool sync = false);

  // Overridden interface methods
  /*! \brief Sets the stream used for image format conversion.
   *  \param[in] stream A reference to the stream used for image format conversion.
   *  \return Returns the status of the function call.
   */
  virtual Status SetStream(tcim::Stream& stream) override;

  /*! \brief Retrieves the stream used for image format conversion.
   *  \return A reference to the stream object.
   */
  virtual tcim::Stream& GetStream() override;

  /*!
   * \brief Synchronizes the stream, ensuring that all tasks in the stream are
   *        completed.
   *
   * \return Returns the status of the function call.
   */
  virtual Status Sync() override;

  /*! \brief Retrieves the total number of input tensors.
   *  \return Returns the total number of input tensors.
   */
  virtual size_t GetInputNum() override;

  /*! \brief Retrieves the total number of output tensors.
   *  \return Returns the total number of output tensors.
   */
  virtual size_t GetOutputNum() override;

  /*! \brief Retrieves input tensor with given index.
   *  \param[in] indx The index of the input tensor.
   *  \return A reference to the input tensor at the specified index.
   */
  virtual tcim::Tensor& GetInputTensor(size_t indx) override;

  /*! \brief Retrieves output tensor with given index.
   *  \param[in] indx The index of the output tensor.
   *  \return A reference to the output tensor at the specified index.
   */
  virtual tcim::Tensor& GetOutputTensor(size_t indx) override;

 private:
  class ColorConverterImpl;
  std::shared_ptr<ColorConverterImpl> impl_;  // PIMPL implementation
};

}  // namespace ImageOps
}  // namespace tcim

#endif
