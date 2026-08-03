const FAVORITES_KEY = "jingjie-ar:favorite-artworks";

export class ArtworkFavorites {
  constructor({ storage } = {}) {
    this.storage = storage;
    this.ids = new Set();
    if (this.storage === undefined) {
      try {
        this.storage = globalThis.localStorage;
      } catch (_) {}
    }
    try {
      const values = JSON.parse(this.storage?.getItem(FAVORITES_KEY) || "[]");
      if (Array.isArray(values)) values.forEach((value) => this.ids.add(String(value)));
    } catch (_) {}
  }

  isFavorite(artworkId) {
    return this.ids.has(String(artworkId));
  }

  toggle(artworkId) {
    const id = String(artworkId);
    const checked = !this.ids.has(id);
    if (checked) this.ids.add(id);
    else this.ids.delete(id);
    try {
      this.storage?.setItem(FAVORITES_KEY, JSON.stringify([...this.ids]));
    } catch (_) {}
    return checked;
  }
}
