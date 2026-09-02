// 仅适配 RK3568 (RKNPU2, aarch64, INT8 量化模型)
// 删除: ZERO_COPY / RV1106 专用分支，只保留 RKNPU2 标准流程

// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "yolov8.h"
#include "common.h"
#include "utils/file_utils.h"
#include "utils/image_utils.h"

static void dump_tensor_attr(rknn_tensor_attr *attr)
{
    printf("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n",
           attr->index, attr->name, attr->n_dims,
           attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
           attr->n_elems, attr->size,
           get_format_string(attr->fmt), get_type_string(attr->type),
           get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

int init_yolov8_model(const char *model_path, rknn_app_context_t *app_ctx)
{
    int ret;
    int model_len = 0;
    char *model;
    rknn_context ctx = 0;

    // 1. 读取 RKNN 模型文件到内存
    model_len = read_data_from_file(model_path, &model);
    if (model == NULL)
    {
        printf("[YOLOv8] load_model fail!\n");
        return -1;
    }

    // 2. 初始化 RKNN 上下文 (RKNPU2 标准接口)
    ret = rknn_init(&ctx, model, model_len, 0, NULL);
    free(model);
    if (ret < 0)
    {
        printf("[YOLOv8] rknn_init fail! ret=%d\n", ret);
        return -1;
    }

    // 3. 查询输入输出数量
    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC)
    {
        printf("[YOLOv8] rknn_query in/out num fail! ret=%d\n", ret);
        return -1;
    }
    printf("[YOLOv8] model input num: %d, output num: %d\n",
           io_num.n_input, io_num.n_output);

    // 4. 查询输入张量属性
    printf("[YOLOv8] input tensors:\n");
    rknn_tensor_attr input_attrs[io_num.n_input];
    memset(input_attrs, 0, sizeof(input_attrs));
    for (int i = 0; i < io_num.n_input; i++)
    {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC)
        {
            printf("[YOLOv8] rknn_query input %d fail! ret=%d\n", i, ret);
            return -1;
        }
        // dump_tensor_attr(&(input_attrs[i]));
    }

    // 5. 查询输出张量属性
    printf("[YOLOv8] output tensors:\n");
    rknn_tensor_attr output_attrs[io_num.n_output];
    memset(output_attrs, 0, sizeof(output_attrs));
    for (int i = 0; i < io_num.n_output; i++)
    {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC)
        {
            printf("[YOLOv8] rknn_query output %d fail! ret=%d\n", i, ret);
            return -1;
        }
        // dump_tensor_attr(&(output_attrs[i]));
    }

    // 6. 保存到 app_ctx
    app_ctx->rknn_ctx = ctx;

    // RK3568 RKNN 模型几乎都是 INT8 非对称量化
    if (output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC &&
        output_attrs[0].type == RKNN_TENSOR_INT8)
    {
        app_ctx->is_quant = true;
    }
    else
    {
        app_ctx->is_quant = false;
    }

    app_ctx->io_num = io_num;
    app_ctx->input_attrs = (rknn_tensor_attr *) malloc(io_num.n_input * sizeof(rknn_tensor_attr));
    memcpy(app_ctx->input_attrs, input_attrs, io_num.n_input * sizeof(rknn_tensor_attr));
    app_ctx->output_attrs = (rknn_tensor_attr *) malloc(io_num.n_output * sizeof(rknn_tensor_attr));
    memcpy(app_ctx->output_attrs, output_attrs, io_num.n_output * sizeof(rknn_tensor_attr));

    // 确定模型输入宽/高/通道
    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW)
    {
        printf("[YOLOv8] model is NCHW input fmt\n");
        app_ctx->model_channel = input_attrs[0].dims[1];
        app_ctx->model_height  = input_attrs[0].dims[2];
        app_ctx->model_width   = input_attrs[0].dims[3];
    }
    else
    {
        printf("[YOLOv8] model is NHWC input fmt\n");
        app_ctx->model_height  = input_attrs[0].dims[1];
        app_ctx->model_width   = input_attrs[0].dims[2];
        app_ctx->model_channel = input_attrs[0].dims[3];
    }
    printf("[YOLOv8] model input height=%d, width=%d, channel=%d, quant=%d\n",
           app_ctx->model_height, app_ctx->model_width,
           app_ctx->model_channel, app_ctx->is_quant);

    return 0;
}

int release_yolov8_model(rknn_app_context_t *app_ctx)
{
    if (app_ctx->input_attrs != NULL)
    {
        free(app_ctx->input_attrs);
        app_ctx->input_attrs = NULL;
    }
    if (app_ctx->output_attrs != NULL)
    {
        free(app_ctx->output_attrs);
        app_ctx->output_attrs = NULL;
    }
    if (app_ctx->rknn_ctx != 0)
    {
        rknn_destroy(app_ctx->rknn_ctx);
        app_ctx->rknn_ctx = 0;
    }
    return 0;
}

int inference_yolov8_model(rknn_app_context_t *app_ctx, image_buffer_t *img,
                           object_detect_result_list *od_results)
{
    int ret;
    image_buffer_t dst_img;
    letterbox_t letter_box;
    rknn_input  inputs[app_ctx->io_num.n_input];
    rknn_output outputs[app_ctx->io_num.n_output];
    const float nms_threshold      = NMS_THRESH;
    const float box_conf_threshold = BOX_THRESH;
    const int bg_color = 114; // letterbox 填充灰: RGB(114,114,114)

    if ((!app_ctx) || !(img) || (!od_results))
        return -1;

    memset(od_results,  0x00, sizeof(*od_results));
    memset(&letter_box, 0,    sizeof(letterbox_t));
    memset(&dst_img,    0,    sizeof(image_buffer_t));
    memset(inputs,      0,    sizeof(inputs));
    memset(outputs,     0,    sizeof(outputs));

    // ============== 1. 预处理：letterbox + resize 到模型尺寸 ==============
    dst_img.width    = app_ctx->model_width;
    dst_img.height   = app_ctx->model_height;
    dst_img.format   = IMAGE_FORMAT_RGB888;
    dst_img.size     = get_image_size(&dst_img);
    dst_img.virt_addr = (unsigned char *) malloc(dst_img.size);
    if (dst_img.virt_addr == NULL)
    {
        printf("[YOLOv8] malloc dst buffer size:%d fail!\n", dst_img.size);
        return -1;
    }

    ret = convert_image_with_letterbox(img, &dst_img, &letter_box, bg_color);
    if (ret < 0)
    {
        printf("[YOLOv8] convert_image_with_letterbox fail! ret=%d\n", ret);
        free(dst_img.virt_addr);
        return -1;
    }

    // ============== 2. 设置输入 (RKNN_TENSOR_UINT8 + NHWC) ==============
    inputs[0].index = 0;
    inputs[0].type  = RKNN_TENSOR_UINT8;
    inputs[0].fmt   = RKNN_TENSOR_NHWC;
    inputs[0].size  = app_ctx->model_width * app_ctx->model_height * app_ctx->model_channel;
    inputs[0].buf   = dst_img.virt_addr;

    ret = rknn_inputs_set(app_ctx->rknn_ctx, app_ctx->io_num.n_input, inputs);
    if (ret < 0)
    {
        printf("[YOLOv8] rknn_inputs_set fail! ret=%d\n", ret);
        free(dst_img.virt_addr);
        return -1;
    }

    // ============== 3. 运行推理 (NPU) ==============
    ret = rknn_run(app_ctx->rknn_ctx, nullptr);
    if (ret < 0)
    {
        printf("[YOLOv8] rknn_run fail! ret=%d\n", ret);
        free(dst_img.virt_addr);
        return -1;
    }

    // ============== 4. 取输出 ==============
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < app_ctx->io_num.n_output; i++)
    {
        outputs[i].index      = i;
        outputs[i].want_float = (!app_ctx->is_quant); // 量化模型保持 int8，自己处理反量化
    }
    ret = rknn_outputs_get(app_ctx->rknn_ctx, app_ctx->io_num.n_output, outputs, NULL);
    if (ret < 0)
    {
        printf("[YOLOv8] rknn_outputs_get fail! ret=%d\n", ret);
        free(dst_img.virt_addr);
        return -1;
    }

    // ============== 5. 后处理 (NMS + letterbox 还原) ==============
    post_process(app_ctx, outputs, &letter_box,
                 box_conf_threshold, nms_threshold, od_results);

    // ============== 6. 释放 RKNN 输出 ==============
    rknn_outputs_release(app_ctx->rknn_ctx, app_ctx->io_num.n_output, outputs);

    // ============== 7. 释放临时缓冲 ==============
    if (dst_img.virt_addr != NULL)
    {
        free(dst_img.virt_addr);
    }

    return ret;
}
