/*
 * YASE: Yet Another Storage Engine
 *
 * CMPT 454 Database Systems II, Fall 2021
 *
 * Copyright (C) School of Computing Science, Simon Fraser University
 *
 * Not for distribution without prior approval.
 */
#include "table.h"
#include "buffer_manager.h"

namespace yase {

Table::Table(std::string name, uint32_t record_size)
  : table_name(name), file(name, record_size), record_size(record_size) {
  // Allocate a new page for the table
  next_free_pid = file.AllocatePage();
}

RID Table::Insert(const char *record) {
  // Obtain buffer manager instance 
  auto *bm = BufferManager::Get();

retry:
  PageId pid = next_free_pid;
  Page *p = bm->PinPage(pid);
  if (!p){
    return RID();
  }
  p->Latch();
  DataPage *dp = p->GetDataPage();
  if (!dp){
    p->Unlatch();
    bm->UnpinPage(p);
    return RID();
  }
  uint32_t slot = 0;
  if (!dp->Insert(record, slot)) {
    latch.lock();
    if (next_free_pid.value != pid.value) {
      latch.unlock();
      p->Unlatch();
      bm->UnpinPage(p);
      goto retry;
    }
    latch.unlock();
    p->Unlatch();
    bm->UnpinPage(p);

    PageId new_page_pid = file.AllocatePage();
    latch.lock();
    next_free_pid = new_page_pid;
    if (!next_free_pid.IsValid()) {
      // Probably no space - return invalid RID
      latch.unlock();
      return RID();
    }
    latch.unlock();
    goto retry;
  }

  p->SetDirty(true);
  p->Unlatch();
  bm->UnpinPage(p);

  // Mark the slot allocation in directory page
  static uint32_t entries_per_dir_page = PAGE_SIZE / sizeof(DirectoryPage::Entry);
  uint32_t dir_page_num = (next_free_pid.GetPageNum() / entries_per_dir_page);
  PageId dir_pid(file.GetDir()->GetId(), dir_page_num);
  p = bm->PinPage(dir_pid);
  if (!p) {
    return RID();
  }
  p->Latch();
  DirectoryPage *dirp = p->GetDirPage();

  uint32_t idx = next_free_pid.GetPageNum() % entries_per_dir_page;
  LOG_IF(FATAL, dirp->entries[idx].free_slots == 0);
  --dirp->entries[idx].free_slots;
  p->SetDirty(true);
  p->Unlatch();
  bm->UnpinPage(p);
  return RID(next_free_pid, slot);
}

bool Table::Read(RID rid, void *out_buf) {
  if (!rid.IsValid() || !file.PageExists(rid.GetPageId())) {
    return false;
  }

  auto *bm = BufferManager::Get();

  // Pin the requested page
  Page *p = bm->PinPage(rid.GetPageId());
  if (!p) {
    return false;
  }
  p->Latch();

  DataPage *dp = p->GetDataPage();
  bool success = dp->Read(rid, out_buf);
  p->Unlatch();
  bm->UnpinPage(p);
  return success;
}

bool Table::Delete(RID rid) {
  if (!rid.IsValid()) {
    return false;
  }

  auto *bm = BufferManager::Get();
  Page *p = bm->PinPage(rid.GetPageId());
  if (!p) {
    return false;
  }
  p->Latch();

  DataPage *dp = p->GetDataPage();
  bool success = dp->Delete(rid);
  if (success) {
    p->SetDirty(true);
  }
  p->Unlatch();
  bm->UnpinPage(p);

  if (success) {
    // Mark the dellocation in directory page
    uint32_t entries_per_dir_page = PAGE_SIZE / sizeof(DirectoryPage::Entry);
    uint32_t dir_page_num = rid.GetPageId().GetPageNum() / entries_per_dir_page;
    PageId dir_pid(file.GetDir()->GetId(), dir_page_num);
    p = bm->PinPage(dir_pid);
    if (!p) {
      return false;
    }
    p->Latch();
    DirectoryPage *dirp = p->GetDirPage();

    uint32_t idx = rid.GetPageId().GetPageNum() % entries_per_dir_page;
    ++dirp->entries[idx].free_slots;
    p->SetDirty(true);
    p->Unlatch();
    bm->UnpinPage(p);
  }

  return success;
}

bool Table::Update(RID rid, const char *record) {
  if (!rid.IsValid()) {
    return false;
  }
  auto *bm = BufferManager::Get();
  Page *p = bm->PinPage(rid.GetPageId());

  if (!p) {
    return false;
  }
  p->Latch();

  DataPage *dp = p->GetDataPage();
  bool success = dp->Update(rid, record);
  if (success) {
    p->SetDirty(true);
  }
  p->Unlatch();
  bm->UnpinPage(p);
  return success;
}
}  // namespace yase
