// MIT License
//
// Copyright (c) 2023 ROCm Developer Tools
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <rocprofiler/fwd.h>
#include <rocprofiler/rocprofiler.h>

#include "lib/rocprofiler/context/context.hpp"
#include "lib/rocprofiler/context/domain.hpp"
#include "lib/rocprofiler/hsa/hsa.hpp"
#include "lib/rocprofiler/registration.hpp"

#include <glog/logging.h>

#include <atomic>
#include <vector>

#define RETURN_STATUS_ON_FAIL(...)                                                                 \
    if(rocprofiler_status_t _status; (_status = __VA_ARGS__) != ROCPROFILER_STATUS_SUCCESS)        \
    {                                                                                              \
        return _status;                                                                            \
    }

namespace rocprofiler
{
namespace callback_tracing
{
namespace
{
#define ROCPROFILER_CALLBACK_TRACING_KIND_STRING(CODE)                                             \
    template <>                                                                                    \
    struct callback_tracing_kind_string<ROCPROFILER_SERVICE_CALLBACK_TRACING_##CODE>               \
    {                                                                                              \
        static constexpr auto value =                                                              \
            std::pair<const char*, size_t>{#CODE, std::string_view{#CODE}.length()};               \
    };

template <size_t Idx>
struct callback_tracing_kind_string;

ROCPROFILER_CALLBACK_TRACING_KIND_STRING(NONE)
ROCPROFILER_CALLBACK_TRACING_KIND_STRING(HSA_API)
ROCPROFILER_CALLBACK_TRACING_KIND_STRING(HIP_API)
ROCPROFILER_CALLBACK_TRACING_KIND_STRING(MARKER_API)
ROCPROFILER_CALLBACK_TRACING_KIND_STRING(CODE_OBJECT)
ROCPROFILER_CALLBACK_TRACING_KIND_STRING(KERNEL_DISPATCH)

template <size_t Idx, size_t... Tail>
std::pair<const char*, size_t>
get_kind_name(rocprofiler_service_callback_tracing_kind_t kind, std::index_sequence<Idx, Tail...>)
{
    if(kind == Idx) return callback_tracing_kind_string<Idx>::value;
    // recursion until tail empty
    if constexpr(sizeof...(Tail) > 0) return get_kind_name(kind, std::index_sequence<Tail...>{});
    return {nullptr, 0};
}
}  // namespace
}  // namespace callback_tracing
}  // namespace rocprofiler

extern "C" {
rocprofiler_status_t
rocprofiler_configure_callback_tracing_service(rocprofiler_context_id_t context_id,
                                               rocprofiler_service_callback_tracing_kind_t kind,
                                               rocprofiler_tracing_operation_t*  operations,
                                               size_t                            operations_count,
                                               rocprofiler_callback_tracing_cb_t callback,
                                               void*                             callback_args)
{
    if(rocprofiler::registration::get_init_status() > -1)
        return ROCPROFILER_STATUS_ERROR_CONFIGURATION_LOCKED;

    if(context_id.handle >= rocprofiler::context::get_registered_contexts().size())
    {
        return ROCPROFILER_STATUS_ERROR_CONTEXT_NOT_FOUND;
    }

    auto& ctx = rocprofiler::context::get_registered_contexts().at(context_id.handle);

    if(!ctx) return ROCPROFILER_STATUS_ERROR_CONTEXT_NOT_FOUND;

    if(!ctx->callback_tracer)
        ctx->callback_tracer = std::make_unique<rocprofiler::context::callback_tracing_service>();

    if(ctx->callback_tracer->callback_data.at(kind).callback)
        return ROCPROFILER_STATUS_ERROR_SERVICE_ALREADY_CONFIGURED;

    RETURN_STATUS_ON_FAIL(rocprofiler::context::add_domain(ctx->callback_tracer->domains, kind));

    ctx->callback_tracer->callback_data.at(kind) = {callback, callback_args};

    for(size_t i = 0; i < operations_count; ++i)
    {
        RETURN_STATUS_ON_FAIL(rocprofiler::context::add_domain_op(
            ctx->callback_tracer->domains, kind, operations[i]));
    }

    return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t
rocprofiler_query_callback_tracing_kind_name(rocprofiler_service_callback_tracing_kind_t kind,
                                             const char**                                name,
                                             uint64_t*                                   name_len)
{
    auto&& val = rocprofiler::callback_tracing::get_kind_name(
        kind, std::make_index_sequence<ROCPROFILER_SERVICE_CALLBACK_TRACING_LAST>{});

    if(name) *name = val.first;
    if(name_len) *name_len = val.second;

    return (val.first) ? ROCPROFILER_STATUS_SUCCESS : ROCPROFILER_STATUS_ERROR_KIND_NOT_FOUND;
}

rocprofiler_status_t
rocprofiler_query_callback_tracing_kind_operation_name(
    rocprofiler_service_callback_tracing_kind_t kind,
    uint32_t                                    operation,
    const char**                                name,
    uint64_t*                                   name_len)
{
    if(kind < ROCPROFILER_SERVICE_CALLBACK_TRACING_NONE ||
       kind >= ROCPROFILER_SERVICE_CALLBACK_TRACING_LAST)
        return ROCPROFILER_STATUS_ERROR_KIND_NOT_FOUND;

    if(kind == ROCPROFILER_SERVICE_CALLBACK_TRACING_HSA_API)
    {
        const auto* val = rocprofiler::hsa::name_by_id(operation);

        if(!val)
        {
            if(name) *name = nullptr;
            if(name_len) *name_len = 0;

            return ROCPROFILER_STATUS_ERROR_OPERATION_NOT_FOUND;
        }

        if(name) *name = val;
        if(name_len) *name_len = strnlen(val, 4096);

        return ROCPROFILER_STATUS_SUCCESS;
    }

    return ROCPROFILER_STATUS_ERROR_NOT_IMPLEMENTED;
}

rocprofiler_status_t
rocprofiler_iterate_callback_tracing_kinds(rocprofiler_callback_tracing_kind_cb_t callback,
                                           void*                                  data)
{
    for(uint32_t i = 0; i < ROCPROFILER_SERVICE_CALLBACK_TRACING_LAST; ++i)
    {
        auto _success = callback(static_cast<rocprofiler_service_callback_tracing_kind_t>(i), data);
        if(_success != 0) break;
    }

    return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t
rocprofiler_iterate_callback_tracing_kind_operations(
    rocprofiler_service_callback_tracing_kind_t      kind,
    rocprofiler_callback_tracing_kind_operation_cb_t callback,
    void*                                            data)
{
    if(kind == ROCPROFILER_SERVICE_CALLBACK_TRACING_HSA_API)
    {
        auto ops = rocprofiler::hsa::get_ids();
        for(const auto& itr : ops)
        {
            auto _success = callback(kind, itr, data);
            if(_success != 0) break;
        }
        return ROCPROFILER_STATUS_SUCCESS;
    }

    return ROCPROFILER_STATUS_ERROR_NOT_IMPLEMENTED;
}

rocprofiler_status_t
rocprofiler_iterate_callback_tracing_kind_operation_args(
    rocprofiler_callback_tracing_record_t            record,
    rocprofiler_callback_tracing_operation_args_cb_t callback,
    void*                                            user_data)
{
    if(record.kind == ROCPROFILER_SERVICE_CALLBACK_TRACING_HSA_API)
    {
        rocprofiler::hsa::iterate_args(
            record.operation,
            *static_cast<rocprofiler_hsa_api_callback_tracer_data_t*>(record.payload),
            callback,
            user_data);
        return ROCPROFILER_STATUS_SUCCESS;
    }

    return ROCPROFILER_STATUS_ERROR_NOT_IMPLEMENTED;
}
}

#undef RETURN_STATUS_ON_FAIL
