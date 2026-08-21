/*
 * Copyright (c) 2022 The Houmo.ai Authors. All rights reserved.
 */

#ifndef __TCIM_STATUS_H__
#define __TCIM_STATUS_H__

namespace tcim {

/*!
 * \brief The return status of the functions.
 */
enum Status : int {
  /*! \brief An undefined or unknown internal error has occurred. */
  ERR_UNDEFINED = -1,
  /*! \brief This function returned with no errors. */
  OK = 0,
  /*! \brief This object or the related resource is not initialized. */
  UNINITIALIZED = 1,
  /*!
   * \brief The index or value passed to the function is out
   * of range.
   */
  OUT_OF_RANGE = 2,
  /*!
   * \brief This function is not yet supported.
   */
  UNSUPPORTED = 3,
  /*!
   * \brief One or more arguments passed to the function are
   * invalid.
   */
  INVALID_ARGUMENT = 4,
  /*!
   * \brief This error occurs when attempting to create an item
   * that already exists.
   */
  ALREADY_EXISTS = 5,
  /*!
   * \brief Permission to perform the function denied.
   */
  PERMISSION_DENIED = 6,
  /*!
   * \brief The requested service or resource is temporarily
   * unavailable.
   */
  UNAVAILABLE = 7,
  /*!
   * \brief Authentication is required to access the resource.
   */
  UNAUTHENTICATED = 8,
  /*!
   * \brief This error occurs when the resource limit, such as memory
   * or disk space, has been reached or exceeded. You can free up
   * resources to accommodate the function.
   */
  RESOURCE_EXHAUSTED = 9,
  /*!
   * \brief A timeout occurred while the function is waiting to be processed.
   */
  TIMEOUT = 10,

  /*!
   * \brief Internal error.
   */
  ERR_KERNEL = 11,
  /*!
   * \brief A critical unrecoverable error occurred during model inference.
   * The IPU core must be reset using the Houmo SMI Tool.
   * See "Houmo SMI Tool User Guide" for details.
   */
  ERR_FATAL = 12,

  /*!
   * \brief The server session has expired, typically because the server
   * restarted and its epoch has changed. All local handles (Models,
   * Buffers, Streams) are stale and must be released before reconnecting.
   */
  SESSION_EXPIRED = 13,

  /*!
   * \brief The runtime is currently in a recovery/reset phase
   * (between RpcServerResetBegin and RpcServerResetEnd). All regular
   * operations are temporarily suspended. Retry after the reset completes.
   */
  RECOVERING = 14,
};

}  // namespace tcim

#endif  // __TCIM_STATUS_H__
