#include "module/scheduler_components/remaster_manager.h"

#include <glog/logging.h>
#include "common/transaction_utils.h"

using std::make_pair;

namespace slog {

RemasterManager::RemasterManager(
    ConfigurationPtr config,
    shared_ptr<Storage<Key, Record>> storage,
    shared_ptr<unordered_map<TxnId, TransactionHolder>> all_txns)
  : config_(config), storage_(storage), all_txns_(all_txns) {}

VerifyMasterResult
RemasterManager::VerifyMaster(const TransactionHolder& txn_holder) {
  auto txn = *txn_holder.txn;
  auto keys = txn_holder.keys_in_partition;
  if (keys.empty()) {
    // None of the keys in this txn is in this partition
    return VerifyMasterResult::VALID;
  }

  auto txn_master_metadata = txn.internal().master_metadata();
  // This should only be the case for testing
  // TODO: add metadata to test cases, make this fatal
  if (txn_master_metadata.empty()) {
    LOG(WARNING) << "Master metadata empty: txn id " << txn.internal().id();
    return VerifyMasterResult::VALID;
  }

  // keeps track of counters that will need to be waited on
  std::unordered_map<Key, int32_t> ahead_counters;
  for (auto pair : keys) {
    auto key = pair.first;
    auto txn_metadata = txn_master_metadata.at(key);

    Record record;
    // TODO: this is not thread safe, the lock is not currently held
    bool found = storage_->Read(key, record);
    if (found) {
      CHECK(txn_master_metadata.contains(key))
              << "Master metadata for key \"" << key << "\" is missing";
      auto stored_metadata = record.metadata;

      if (txn_metadata.counter() > stored_metadata.counter) {
        ahead_counters[key] = txn_metadata.counter();
      } else if (txn_metadata.counter() < stored_metadata.counter) {
        return VerifyMasterResult::ABORT;
      } else {
        CHECK(txn_metadata.master() == stored_metadata.master)
                << "Metadata is invalid, different masters with the same counter";
      }
    } else { // key not in storage
      if (txn_metadata.counter() > 0) {
        ahead_counters[key] = txn_metadata.counter();
      }
    }
  }

  // Check if no keys are blocked at all
  if (ahead_counters.empty()){
    auto no_key_indirectly_blocked = true;
    for (auto key : txn_holder.keys_in_partition) {
      if (indirectly_blocked_queue.count(key.first)) {
        no_key_indirectly_blocked = false;
        break;
      }
    }
    if (no_key_indirectly_blocked) {
      return VerifyMasterResult::VALID;
    }
  }

  // add the txn to the queue for each key
  for (auto key : txn_holder.keys_in_partition) {
    // key is waiting on remaster, directly blocked
    if (ahead_counters.count(key.first)) {
      auto counter = ahead_counters[key.first];
      InsertIntoBlockedQueue(key.first, counter, txn);
    } else { // key is not blocked by remaster
      auto txn_id = txn.internal().id();
      indirectly_blocked_queue[key.first].push_back(txn_id);
    }
  }
  return VerifyMasterResult::WAITING;
}

void RemasterManager::InsertIntoBlockedQueue(const Key key, const uint32_t counter, const Transaction& txn) {
  auto txn_id = txn.internal().id();
  auto entry = make_pair(txn_id, counter);

  // Iterate until at end or counter is smaller than next element. Maintains priority queue
  auto itr = blocked_queue_[key].begin();
  while (itr != blocked_queue_[key].end() && (*itr).second >= counter) {
    itr++;
  }
  blocked_queue_[key].insert(itr, entry);
}

list<TxnId> RemasterManager::RemasterOccured(Key key, const uint32_t remaster_counter) {
  list<TxnId> unblocked;
  list<TxnId> shouldAbort;

  // No txns waiting for this remaster
  if (blocked_queue_.count(key) == 0) {
    return unblocked;
  }

  // If this queue already has elements, then no txns will be unblocked
  auto indirectly_blocked_queue_empty = (indirectly_blocked_queue.count(key) == 0);

  // Move keys to indirectly_blocked_queue
  auto moved_keys = false;
  auto itr = blocked_queue_[key].begin();
  while (itr != blocked_queue_[key].end() && (*itr).second <= remaster_counter) {
    auto txn_id = (*itr).first;
    if ((*itr).second < remaster_counter) {
      // TODO abort these
      LOG(FATAL) << "Remaster is invalidating txns already in the queue";
    }
    indirectly_blocked_queue[key].push_back(txn_id);
    moved_keys = true;
    itr = blocked_queue_[key].erase(itr);
  }

  // Try to unblock txns
  if (indirectly_blocked_queue_empty && moved_keys) {
    TryToUnblock(key, unblocked);
  }
  
  return unblocked;
}

void RemasterManager::TryToUnblock(const Key unblocked_key, list<TxnId>& unblocked) {
  if (indirectly_blocked_queue.count(unblocked_key) == 0) {
    return;
  }

  auto txn_id = indirectly_blocked_queue.at(unblocked_key).front(); // size > 0, otherwise queue is deleted
  auto keys = all_txns_->at(txn_id).keys_in_partition;
  
  // Check if txn is front of queue for all keys
  for (auto key : keys) {
    if (indirectly_blocked_queue.count(key.first) == 0 || indirectly_blocked_queue.at(key.first).front() != txn_id) {
      return;
    }
  }

  // Remove unblocked txn from queues
  for (auto key : keys) {
    indirectly_blocked_queue.at(key.first).pop_front();
  }
  unblocked.push_back(txn_id);

  // Garbage collect empty queues
  for (auto key : keys) {
    if (indirectly_blocked_queue.at(key.first).empty()) {
      indirectly_blocked_queue.erase(key.first);
    }
  }

  // Recurse on updated queues
  for (auto key : keys) {
    if (indirectly_blocked_queue.count(key.first)) {
      TryToUnblock(key.first, unblocked);
    }
  }
}

} // namespace slog