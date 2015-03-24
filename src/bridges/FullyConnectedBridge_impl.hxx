//
//  FullyConnectedBridge_impl.hxx
//  moka
//
//  Created by Firas Abuzaid on 2/12/15.
//  Copyright (c) 2015 Hazy Research. All rights reserved.
//

#ifndef moka_FullyConnectedBridge_impl_hxx
#define moka_FullyConnectedBridge_impl_hxx

// Constructor for fully connected layer
template <typename DataType, typename DriverClass>
FullyConnectedBridge<DataType, Layout_CRDB, DataType, Layout_CRDB, DriverClass>::
FullyConnectedBridge(InputLayerType * const _p_input_layer, OutputLayerType * const _p_output_layer,
  const cnn::LayerParameter * const _layer_param, const cnn::SolverParameter * const _solver_param,
  DriverClass * const _p_driver) : AbstractBridge<DataType, Layout_CRDB, DataType, Layout_CRDB,
  DriverClass>(_p_input_layer, _p_output_layer, _layer_param, _solver_param, _p_driver),
  // padding is set to 0, and stride is set to 1. iC would also work as the
  // value set to K. (We assert that they are equal in initialize.)
  K(iR), num_output_features(layer_param->inner_product_param().num_output()),
  stride(1), padding(0), bias_term(layer_param->inner_product_param().bias_term()),
  weight_filler(layer_param->inner_product_param().weight_filler()),
  bias_filler(layer_param->inner_product_param().bias_filler()) {

  report_forward_constructor.reset();
  report_forward_last_transfer.reset();
  report_forward_kernel.reset();
  report_forward_history.reset();
  report_forward_lowering.reset();
  report_backward_inverse_lowering.reset();
  report_backward_grad_kernel.reset();
  report_backward_weight_kernel.reset();

#ifdef _DO_ASSERT
  assert(oR == (iR + 2 * padding - K) / stride + 1);
  assert(oC == (iC + 2 * padding - K) / stride + 1);
  assert(iB == oB); assert(num_output_features == oD);
#endif

  p_model_cube = new LogicalCubeType(NULL, K, K, iD, num_output_features);

  p_model_cube_shadow = new LogicalCubeType(K, K, iD, num_output_features);
  initialize_logical_cube(p_model_cube_shadow, weight_filler);

  p_model_gradient_cube = new LogicalCubeType(K, K, iD, num_output_features);
  p_model_gradient_cube->reset_cube();

  if (bias_term) {
    p_bias_cube = new LogicalCubeType(1, 1, num_output_features, 1);
    initialize_logical_cube(p_bias_cube, bias_filler);

    p_bias_gradient_cube = new LogicalCubeType(1, 1, num_output_features, 1);
  }

  // First, allocate the space we need for lowering
  // Following code is very messy without the Matrix interface -- TODO
  p_forward_lowered_data = new LogicalCube<DataType, Layout_CRDB>(K*K*iD, oR*oC*iB,
      1, 1);

  LogicalCube<DataType, Layout_CRDB> lowered_forward_model(p_model_cube_shadow->get_p_data(), num_output_features,
      K*K*iD, 1, 1);

  LogicalCube<DataType, Layout_CRDB> lowered_forward_output(p_output_layer->p_data_cube->get_p_data(),
      num_output_features, oR*oC*iB, 1, 1);

  p_forward_lower_connector = new Connector<DataType, Layout_CRDB, DataType, Layout_CRDB,
                            LOWERING_TYPE1, DriverClass>(p_input_layer->p_data_cube, p_forward_lowered_data, K,
                                padding, stride, p_driver);

  p_forward_gemm_kernel = new Kernel<DataType, Layout_CRDB, DataType, Layout_CRDB, DataType, Layout_CRDB,
                        Kernel_GEMM_OpenBlas, KernelConfig_GEMM_NOTRANS_NOTRANS>(&lowered_forward_model,
                            p_forward_lowered_data, &lowered_forward_output, p_driver);

  p_backward_inputgrad = new LogicalCube<DataType, Layout_CRDB>(K*K*iD, oR*oC*iB, 1, 1);

  p_backward_gemm_updateweight_kernel = new Kernel<DataType, Layout_CRDB, DataType, Layout_CRDB, DataType,
                                      Layout_CRDB, Kernel_GEMM_OpenBlas,
                                      KernelConfig_GEMM_NOTRANS_TRANS>(&lowered_forward_output,
                                          p_forward_lowered_data, &lowered_forward_model, p_driver);
  p_backward_gemm_updateweight_kernel->alpha = 1.0;
  p_backward_gemm_updateweight_kernel->beta = 0.0;

  p_backward_gemm_updategrad_kernel = new Kernel<DataType_SFFloat, Layout_CRDB, DataType_SFFloat, Layout_CRDB,
                                    DataType_SFFloat, Layout_CRDB, Kernel_GEMM_OpenBlas,
                                    KernelConfig_GEMM_TRANS_NOTRANS>(&lowered_forward_model,
                                        &lowered_forward_output, p_backward_inputgrad, p_driver);

  report_forward_constructor.end(0, 0, 0);
}

// Intiailize a Logical Cube using a FillerParameter. This is only called if layer_param is
// non-NULL.
template <typename DataType, typename DriverClass>
void FullyConnectedBridge<DataType, Layout_CRDB, DataType, Layout_CRDB, DriverClass>::
initialize_logical_cube(const LogicalCubeType * cube, const cnn::FillerParameter filler_param) {
  const string type = filler_param.type();
  if (type == "constant") {
    Util::constant_initialize<DataType>(cube->get_p_data(), (DataType) filler_param.value(), cube->n_elements);
  } else if (type == "xavier") {
    Util::xavier_initialize(cube->get_p_data(), cube->n_elements, cube->B);
  } else if (type == "bernoulli") {
    Util::bernoulli_initialize(cube->get_p_data(), cube->n_elements, filler_param.value());
  } else if (type == "gaussian") {
    Util::gaussian_initialize(cube->get_p_data(), cube->n_elements, (DataType) filler_param.mean(),
        (DataType) filler_param.std());
  } else {
    cout << "ERROR! INITIALIZATION TYPE NOT SUPPORTED!" << endl;
    assert(false);
  }
}

/**
 * This function does the following:
 *
 * First Layer {iData, iModel, iGrad}
 * Next Layer {oData, oModel, oGrad}
 *
 * Procedure:
 *
 * (1) iData -----lowering-----> LoweredData
 *
 * (2) LoweredData x iModel -----------> oData
 *
 **/
template <typename DataType, typename DriverClass>
void FullyConnectedBridge<DataType, Layout_CRDB, DataType, Layout_CRDB, DriverClass>::
forward() {
  Util::set_num_threads(run_with_n_threads);

  // Copy input to Device. This should be refactor'ed out into the
  // scheduler.
  DeviceMemoryPointer_Local_RAM plocal(p_input_layer->p_data_cube->get_p_data(),
    input_d_cube->n_elements*sizeof(DataType));
  DeviceMemoryPointer * phost = p_driver->get_device_pointer(input_d_cube->get_p_data(),
    input_d_cube->n_elements*sizeof(DataType));
  p_driver->memcpy(phost, &plocal);

  report_forward_last_transfer.reset();

  ////////////////////////////////////////////////////////////////////////////////
  if (p_model_cube->get_p_data() == NULL) {
    p_model_cube->set_p_data(p_model_cube_shadow->get_p_data());
  }

  // (0) cast input model and output to matrix
  // This one should be refactored with the matrix interface
  LogicalCube<DataType, Layout_CRDB> lowered_model(p_model_cube->get_p_data(), num_output_features,
      K*K*iD, 1, 1);
  LogicalCube<DataType, Layout_CRDB> lowered_output(output_d_cube->get_p_data(),
      num_output_features, oR*oC*iB, 1, 1);

  // (1) do the lowering
  p_forward_lower_connector->lower_cube(input_d_cube, p_forward_lowered_data);

  // (2) call GEMM kernel
  p_forward_gemm_kernel->compute(&lowered_model, p_forward_lowered_data, &lowered_output);

  // Right now the output we get is of the form:
  // [(b_0, d_0), (b_1, d_0), ... , (b_n, d_0)
  //
  //  (b_0, d_m), (b_1, d_m), ... , (b_n, d_m)]
  //  we need to transpose this, so that the outputs
  //  of a single batch are contiguous in memory.
  //  For now, we will call remap_output to fix this
  //  issue.
  //
  //  TODO: figure out how to properly transpose the
  //  inputs so that we get the correct output without
  //  needing to call remap
  p_forward_lower_connector->remap_output(*output_d_cube, num_output_features, iB, oR*oC);

  // add bias
  if (bias_term) {
    DeviceMemoryPointer * output = output_d_cube->get_device_pointer(p_driver);
    DeviceMemoryPointer * bias = p_bias_cube->get_device_pointer(p_driver);

    _bias_forward_arg_helper _arg1;
    _arg1.src_skip = oR*oC*sizeof(DataType);
    _arg1.DataTypeSize = sizeof(DataType);
    _arg1.oD = oD;

    size_t ORxOC = oR*oC;

    DeviceMemoryPointer * arg1 = p_driver->get_device_pointer((void*)&_arg1,
      sizeof(_bias_forward_arg_helper));

    DeviceMemoryPointer * arg2 = p_driver->get_device_pointer((void*)&ORxOC,
        sizeof(size_t));

    p_driver->template parallel_map<_f_src_to_dst_bias_forward,
      _f_bias_forward>(bias, output, _arg1.src_skip, arg1, arg2);
  }
  ////////////////////////////////////////////////////////////////////////////////

  // Copy output to Host. This should be refactor'ed out into the
  // scheduler.
  DeviceMemoryPointer_Local_RAM plocal2(p_output_layer->p_data_cube->get_p_data(),
    output_d_cube->n_elements*sizeof(DataType));
  DeviceMemoryPointer * phost2 = p_driver->get_device_pointer(output_d_cube->get_p_data(),
    output_d_cube->n_elements*sizeof(DataType));
  p_driver->memcpy(&plocal2, phost2);

  report_forward_last_transfer.end();
  report_forward_last_transfer.aggregate_onlystat(p_forward_gemm_kernel->report_last_lowering);
  report_forward_last_transfer.aggregate_onlystat(p_forward_lower_connector->report_last_lowering);

  report_forward_history.aggregate(report_forward_last_transfer);
  report_forward_kernel.aggregate(p_forward_gemm_kernel->report_last_lowering);
  report_forward_lowering.aggregate(p_forward_lower_connector->report_last_lowering);
}

/**
  * This function does the following:
  *
  * First Layer {iData, iModel, iGrad}
  * Next Layer {oData, oModel, oGrad}
  *
  * Procedure:
  *
  * (1) oData element-wise-mul oGrad -------> BackPropogatedGradient
  *
  * (2) Update iGrad:
  *
  * (2.1) iModel x BackPropogatedGradient -----------> LoweredGradient_for_iData
  *
  * (2.2) LoweredGradient_for_iData ----inverse_of_lowering----> iGrad
  *
  * (3) BackPropogatedGradient x Lowered_iData * stepsize + iModel ---------> New iModel
  *
 **/
template <typename DataType, typename DriverClass>
void FullyConnectedBridge<DataType, Layout_CRDB, DataType, Layout_CRDB, DriverClass>::
backward() {
  Util::set_num_threads(run_with_n_threads);

  report_backward_updateweight_last_transfer.reset();

  // (2) calculate the GEMM between the gradient of output and old kernel to calc the update on grad
  // Note: lowered_model is storing p_model_cube_history, not p_model_cube. We need this for the momentum
  // update.
  LogicalCube<DataType, Layout_CRDB> lowered_model(p_model_cube->get_p_data(), num_output_features, K*K*iD, 1, 1);
  LogicalCube<DataType, Layout_CRDB> lowered_model_grad(p_model_gradient_cube->get_p_data(), num_output_features, K*K*iD, 1, 1);
  LogicalCube<DataType, Layout_CRDB> lowered_outputgrad(p_output_layer->p_gradient_cube->get_p_data(),
      num_output_features, oR*oC*iB, 1, 1);

  // (3) update the bias term, summing over the gradients for each O and B
  if (bias_term) {
    const size_t output_feature_size = oR*oC;
    p_bias_gradient_cube->reset_cube();
    DataType * const bias_term = p_bias_gradient_cube->get_p_data();
    for (size_t o_b = 0; o_b < oB; ++o_b) {
      for (size_t o_d = 0; o_d < oD; ++o_d) {
        const LogicalMatrix<DataType> input_grad_slice = p_output_layer->p_gradient_cube->get_logical_matrix(o_d, o_b);
        DataType sum = DataType(0.0);
        for (size_t i = 0; i < output_feature_size; ++i) {
          sum += input_grad_slice.p_data[i];
        }
        bias_term[o_d] += sum;
      }
    }
  }
  // Here, we again call remap_output, but we do so BEFORE calling compute and inverse_lower_cube
  p_output_layer->p_gradient_cube->template remap_output<LOWERING_TYPE1>(oB, num_output_features, oR*oC, p_driver);
  //    - 2.1 GEMM between the gradient of output and old kernel
  p_backward_gemm_updategrad_kernel->compute(&lowered_model, &lowered_outputgrad, p_backward_inputgrad);
  //    - 2.2 undo the lowering (i.e., sum together all grad corresponding to the same unlowered position)
  p_forward_lower_connector->inverse_lower_cube(p_backward_inputgrad, p_input_layer->p_gradient_cube);
  // (4) calculate the GEMM between the gradient of output and lowered data to calc the update on kernel
  p_backward_gemm_updateweight_kernel->alpha = 1.0;
  p_backward_gemm_updateweight_kernel->beta = 0.0;
  // Performing weight update:

  p_backward_gemm_updateweight_kernel->compute(&lowered_outputgrad, p_forward_lowered_data, &lowered_model_grad);

  report_backward_updateweight_last_transfer.end();

  report_backward_updateweight_last_transfer.aggregate_onlystat(p_backward_gemm_updategrad_kernel->report_last_lowering);
  report_backward_updateweight_last_transfer.aggregate_onlystat(p_forward_lower_connector->report_last_inverse_lowering);
  report_backward_updateweight_last_transfer.aggregate_onlystat(p_backward_gemm_updateweight_kernel->report_last_lowering);

  report_backward_inverse_lowering.aggregate(p_forward_lower_connector->report_last_inverse_lowering);
  report_backward_weight_kernel.aggregate(p_backward_gemm_updateweight_kernel->report_last_lowering);
  report_backward_grad_kernel.aggregate(p_backward_gemm_updategrad_kernel->report_last_lowering);
  report_backward_updateweight_history.aggregate(report_backward_updateweight_last_transfer);
}

template <typename DataType, typename DriverClass>
FullyConnectedBridge<DataType, Layout_CRDB, DataType, Layout_CRDB, DriverClass>::
~FullyConnectedBridge() {
  if (bias_term) {
    delete p_bias_cube;
    delete p_bias_gradient_cube;
  }
  delete p_model_cube_shadow; delete p_model_gradient_cube; delete p_forward_lowered_data;
  delete p_backward_gemm_updategrad_kernel; delete p_backward_gemm_updateweight_kernel;
  delete p_backward_inputgrad; delete p_forward_gemm_kernel;
  delete p_forward_lower_connector;
}

#endif
