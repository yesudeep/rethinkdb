#include "btree/append_prepend.hpp"
#include "btree/modify_oper.hpp"
#include "buffer_cache/co_functions.hpp"

struct btree_append_prepend_oper_t : public btree_modify_oper_t {

    btree_append_prepend_oper_t(boost::shared_ptr<data_provider_t> _data, bool _append)
        : data(_data), append(_append)
    { }

    bool operate(const boost::shared_ptr<transaction_t>& txn, scoped_malloc<btree_value_t>& value) {
        if (!value) {
            result = apr_not_found;
            return false;
        }

        size_t new_size = value->value_size() + data->get_size();
        if (new_size > MAX_VALUE_SIZE) {
            result = apr_too_large;
            return false;
        }

        blob_t b(txn->get_cache()->get_block_size(), value->value_ref(), blob::btree_maxreflen);
        buffer_group_t buffer_group;
        blob_acq_t acqs;

        size_t old_size = b.valuesize();
        if (append) {
            b.append_region(txn.get(), data->get_size());
            b.expose_region(txn.get(), rwi_write, old_size, data->get_size(), &buffer_group, &acqs);
        } else {
            b.prepend_region(txn.get(), data->get_size());
            b.expose_region(txn.get(), rwi_write, 0, data->get_size(), &buffer_group, &acqs);
        }

        data->get_data_into_buffers(&buffer_group);
        result = apr_success;
        return true;
    }

    int compute_expected_change_count(const size_t block_size) {
        if (data->get_size() < MAX_IN_NODE_VALUE_SIZE) {
            return 1;
        } else {
            size_t size = ceil_aligned(data->get_size(), block_size);
            // one for the leaf node plus the number of blocks required to hold the large value
            return 1 + size / block_size;
        }
    }

    void actually_acquire_large_value(large_buf_t *lb) {
        if (append) {
            co_acquire_large_buf_rhs(lb);
        } else {
            co_acquire_large_buf_lhs(lb);
        }
    }

    append_prepend_result_t result;

    boost::shared_ptr<data_provider_t> data;
    bool append;   // true = append, false = prepend
};

append_prepend_result_t btree_append_prepend(const store_key_t &key, btree_slice_t *slice, boost::shared_ptr<data_provider_t> data, bool append, castime_t castime, order_token_t token) {
    btree_append_prepend_oper_t oper(data, append);
    run_btree_modify_oper(&oper, slice, key, castime, token);
    return oper.result;
}
