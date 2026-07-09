#ifndef PALA_STORAGE_LIST_ITEMS_H
#define PALA_STORAGE_LIST_ITEMS_H

#include "src/storage/kv_store.h"
#include "src/pure/list_codec.h"

// Testable KV-store-backed API for the todo list.
ListData loadList(KeyValueStore& kv);
void     saveList(KeyValueStore& kv, const ListData& data);

#ifdef ARDUINO
// ----------------------------------------------------------------------------
// Firmware-side runtime state. The pure ListData (above) is the on-disk
// representation; ListState is the in-memory working copy used by the list
// screen (it adds `selectedIndex` for UI navigation). Loaded from / saved
// to the KV-store by loadListItems() / saveListItems() below.
// ----------------------------------------------------------------------------
struct ListItem {
  char text[MAX_LIST_TEXT + 1];
  uint8_t done = 0;
};

struct ListState {
  ListItem items[MAX_LIST_ITEMS];
  int count = 0;
  int selectedIndex = 0;
};

extern ListState g_list;

// Firmware glue — drives `g_list`.
void sanitizeListText(String& s);  // re-export the codec helper at the firmware name
void loadListItems();
void saveListItems();
bool listHasVisibleItems();
#endif

#endif  // PALA_STORAGE_LIST_ITEMS_H
