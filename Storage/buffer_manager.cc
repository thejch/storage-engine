/*
 * YASE: Yet Another Storage Engine
 *
 * CMPT 454 Database Systems II Course Project, Fall 2021
 *
 * Copyright (C) School of Computing Science, Simon Fraser University
 *
 * Not for distribution without prior approval.
 */
#include "buffer_manager.h"

namespace yase {

BufferManager *BufferManager::instance = nullptr;

// Initialize a new buffer manager
BufferManager::BufferManager(uint32_t page_count) {
  // Allocate and initialize memory for page frames
  // 1. Initialize the page_count member variable
  // 2. malloc the desired amount of memory (specified by page_count) 
  //    and store the address in member variable page_frames
  // 3. Clear the allocated memory to 0
  // 4. Use placement new to initialize each frame
  latch.lock();
  this->page_count = page_count;
  page_frames = (Page*)calloc(page_count, sizeof(Page));
  // page_frames = (Page*)malloc(page_count * sizeof(Page));
  // memset(page_frames, 0, page_count * sizeof(Page));
  // page_frames = new Page();
  for (unsigned int i=0; i<page_count; i++){
    Page* initialized_page = new (page_frames + i) Page();
    lru_queue.push_back(initialized_page);
  }
  latch.unlock();

}

BufferManager::~BufferManager() {
  // Flush all dirty pages and free page frames
  latch.lock();

  for (unsigned int i=0; i<page_count; i++){
    Page* page = page_frames + i;
    page->latch.lock();
    if (page->IsDirty()){
      PageId page_id = page->GetPageId();
      page->latch.unlock();
      uint16_t bf_id = page_id.GetFileID();
      page->latch.lock();
      file_map[bf_id]->FlushPage(page_id, page);
      page->latch.unlock();
    } else {
      page->latch.unlock();
    }
  }
  free(page_frames);
  latch.unlock();
}

Page* BufferManager::PinPage(PageId page_id) {
  // Pin a page in the buffer pool:
  // 1. Check if the page already exists in the buffer pool (using the 
  //    PageID - Page* mapping). If so, return it.
  // 2. If not, find a free page frame [p] using LRU and:
  //    (a) setup the PageID - [p] mapping
  //    (b) load the page from storage using the FileID - BaseFile* mapping.
  //    frame and set up the PageID-Page Frame mapping
  //
  // Notes: 
  // 1. Before returning, make sure increment the page's pin_count (using
  // Page::IncPinCount function) and record page_id in the Page structure.
  //
  // 2. The pinned page should not appear in the LRU queue.
  //
  // 3. Errors such as invalid page IDs should be handled. If there is any error
  //    at any step, return nullptr.
  bool ret = false;
  if (!page_id.IsValid()){
    return nullptr;
  }
  // LOG(ERROR) << "PinPage 1";

  latch.lock();
  if (page_map.count(page_id)){ // if page_id is in page_map (the buffer pool)
    Page* pinned_page = page_map[page_id];
    // LOG(ERROR) << "PinPage 2";
    latch.unlock();
    if (!pinned_page) {
      return nullptr;
    }
    // LOG(ERROR) << "PinPage 3";
    pinned_page->latch.lock();
    pinned_page->IncPinCount();
    // pinned_page->latch.unlock();

    // if the page is in the LRU queue, remove it from the queue
    latch.lock();
    // pinned_page->latch.lock();
    // LOG(ERROR) << "PinPage 4";
    std::list<Page*>::iterator page_it = std::find(lru_queue.begin(), lru_queue.end(), pinned_page);
    pinned_page->latch.unlock();
    // LOG(ERROR) << "PinPage 5";
    if (page_it != lru_queue.end()){
      lru_queue.erase(page_it);
    }
    // LOG(ERROR) << "PinPage 6";
    latch.unlock();

    return pinned_page;

  } else { // if page_id is not in page_map (the buffer pool)
    // latch.unlock();
    Page* page_buffer;
    // latch.lock();
    // LOG(ERROR) << "PinPage 7";
    if (page_map.size() >= page_count) { // if the buffer pool is full, evict a page, and load the new page into that page frame
      // LOG(ERROR) << "PinPage 8";
      if (lru_queue.empty()) {
        latch.unlock();
        return nullptr;
      }
      // LOG(ERROR) << "PinPage 9";
      Page* evicted_page = lru_queue.front();
      // LOG(ERROR) << "PinPage 10";
      lru_queue.pop_front();
      latch.unlock();
      if (!evicted_page) {
        return nullptr;
      }
      // LOG(ERROR) << "PinPage 11";
      evicted_page->latch.lock();
      PageId evicted_page_id = evicted_page->GetPageId();
      // LOG(ERROR) << "PinPage 12";
      if (!evicted_page_id.IsValid()){
        evicted_page->latch.unlock();
        return nullptr;
      }
      // evicted_page->latch.unlock();
      // LOG(ERROR) << "PinPage 13";
      uint16_t evicted_bf_id = evicted_page_id.GetFileID();
      // evicted_page->latch.lock();
      // LOG(ERROR) << "PinPage 14";
      if (evicted_page->IsDirty()){
        latch.lock();
        // LOG(ERROR) << "PinPage 15";
        ret = file_map[evicted_bf_id]->FlushPage(evicted_page_id, evicted_page);
        latch.unlock();
        evicted_page->latch.unlock();
        if (!ret){
          return nullptr;
        }
      } else {
        evicted_page->latch.unlock();
      }
      latch.lock();
      // LOG(ERROR) << "PinPage 16";
      page_map.erase(evicted_page_id);
      latch.unlock();
      // LOG(ERROR) << "PinPage 17";
      page_buffer = evicted_page; // we are going to load the new page in here
      if (!page_buffer) {
        return nullptr;
      }
    } else { // if the buffer pool is not full, find an empty page frame in it, and load the new page into that page
      // LOG(ERROR) << "PinPage 18";
      if (lru_queue.empty()) {
        latch.unlock();
        return nullptr;
      }
      // LOG(ERROR) << "PinPage 19";
      page_buffer = lru_queue.front();
      // LOG(ERROR) << "PinPage 20";
      lru_queue.pop_front();
      // LOG(ERROR) << "PinPage 21";
      latch.unlock();
      if (!page_buffer) {
        return nullptr;
      }
    }

    // LOG(ERROR) << "PinPage 22";
    uint16_t bf_id = page_id.GetFileID();
    latch.lock();
    // LOG(ERROR) << "PinPage 23";
    BaseFile* bf = file_map[bf_id];
    if (!bf){
      latch.unlock();
      return nullptr;
    }
    // LOG(ERROR) << "PinPage 24";
    latch.unlock();
    page_buffer->latch.lock();
    ret = bf->LoadPage(page_id, page_buffer);
    // LOG(ERROR) << "PinPage 25";
    page_buffer->latch.unlock();
    if (!ret){
      return nullptr;
    }
    page_buffer->latch.lock();
    latch.lock();
    page_map[page_id] = page_buffer;
    // LOG(ERROR) << "PinPage 26";
    latch.unlock();
    page_buffer->pin_count = 1;
    page_buffer->page_id = page_id;
    page_buffer->latch.unlock();
    // LOG(ERROR) << "PinPage 27";
    
    return page_buffer;
  }

  // The following return statement tries to silent compiler warning so the base
  // code compiles, you may need to revise/remove it in your implementation
  return nullptr;
}

void BufferManager::UnpinPage(Page *page) {
  // 1. Unpin the provided page by decrementing the pin count. The page should
  // stay in the buffer pool until another operation evicts it later.
  // 2. The page should be added to the LRU queue when its pin count becomes 0.
  //
  // Note: you may assume page is non-null.
  page->latch.lock();
  page->DecPinCount();
  if (page->pin_count == 0){
    page->latch.unlock();
    latch.lock();
    lru_queue.push_back(page);  
    latch.unlock();
  }
  page->latch.unlock();
}

void BufferManager::RegisterFile(BaseFile *bf) {
  // Setup a mapping from [bf]'s file ID to [bf]
  latch.lock();
  file_map[bf->GetId()] = bf;
  latch.unlock();
}

}  // namespace yase