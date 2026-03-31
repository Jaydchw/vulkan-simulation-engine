#pragma once
#include <unordered_map>
#include <vector>

#include "Entity.h"

// Dense, cache-coherent component storage backed by two parallel vectors:
//   ents  – flat array of owning Entity IDs  (contiguous in memory)
//   comps – flat array of component values   (contiguous in memory)
//
// A hash map provides O(1) index lookup so getComponent() stays fast.
// Deletion uses swap-remove to keep the arrays dense at all times.
//
// Range-for iteration yields (entity, component) pairs via lightweight
// proxy structs.  Const iteration requires `const auto& [e, c]` or
// `auto&&`; mutable iteration requires `auto&&`.
template <typename T>
class ComponentStore {
 public:
  // ── Proxy types returned by iterators ──────────────────────────────────────
  struct ConstEntry {
    Entity     entity;
    const T&   component;
  };
  struct MutEntry {
    Entity  entity;
    T&      component;
  };

  // ── Iterators ───────────────────────────────────────────────────────────────
  struct const_iterator {
    const ComponentStore* store;
    size_t i;
    ConstEntry operator*() const { return {store->ents[i], store->comps[i]}; }
    const_iterator& operator++() { ++i; return *this; }
    bool operator!=(const const_iterator& o) const { return i != o.i; }
  };
  struct iterator {
    ComponentStore* store;
    size_t i;
    MutEntry operator*() const { return {store->ents[i], store->comps[i]}; }
    iterator& operator++() { ++i; return *this; }
    bool operator!=(const iterator& o) const { return i != o.i; }
  };

  const_iterator begin() const { return {this, 0}; }
  const_iterator end()   const { return {this, comps.size()}; }
  iterator       begin()       { return {this, 0}; }
  iterator       end()         { return {this, comps.size()}; }

  // ── Mutation ────────────────────────────────────────────────────────────────
  void insert(Entity entity, T component) {
    auto [it, inserted] =
        idx.emplace(entity, static_cast<uint32_t>(comps.size()));
    if (inserted) {
      ents.push_back(entity);
      comps.push_back(std::move(component));
    } else {
      comps[it->second] = std::move(component);
    }
  }

  // Swap-remove: moves the last element into the deleted slot so the
  // arrays remain dense and no holes are left.
  void erase(Entity entity) {
    auto it = idx.find(entity);
    if (it == idx.end()) return;
    const uint32_t i    = it->second;
    const uint32_t last = static_cast<uint32_t>(comps.size()) - 1u;
    if (i != last) {
      ents[i]      = ents[last];
      comps[i]     = std::move(comps[last]);
      idx[ents[i]] = i;
    }
    ents.pop_back();
    comps.pop_back();
    idx.erase(it);
  }

  // ── Lookup ──────────────────────────────────────────────────────────────────
  T*       get(Entity entity)       {
    auto it = idx.find(entity);
    return it != idx.end() ? &comps[it->second] : nullptr;
  }
  const T* get(Entity entity) const {
    auto it = idx.find(entity);
    return it != idx.end() ? &comps[it->second] : nullptr;
  }

  bool   has(Entity entity) const { return idx.count(entity) > 0; }
  size_t size()             const { return comps.size(); }
  bool   empty()            const { return comps.empty(); }

  // ── Direct dense-array access (e.g. for TimelineSystem) ────────────────────
  const std::vector<Entity>& entityList()    const { return ents; }
  const std::vector<T>&      componentList() const { return comps; }
  std::vector<T>&            componentListMut()    { return comps; }

 private:
  std::vector<Entity>                  ents;
  std::vector<T>                       comps;
  std::unordered_map<Entity, uint32_t> idx;
};
