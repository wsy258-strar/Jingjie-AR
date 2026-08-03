export function buildArtworkShareUrl(location, artworkId) {
  const url = new URL(location?.href || location);
  url.searchParams.set("artwork", String(artworkId));
  return url.toString();
}

export async function copyArtworkShareLink({
  navigatorObject = globalThis.navigator,
  documentObject = globalThis.document,
  url
} = {}) {
  const clipboard = navigatorObject?.clipboard;
  if (typeof clipboard?.writeText === "function") {
    try {
      await clipboard.writeText(url);
      return true;
    } catch (_) {}
  }

  let textarea;
  try {
    if (!documentObject?.createElement || !documentObject.body?.appendChild || !documentObject.execCommand) {
      return false;
    }
    textarea = documentObject.createElement("textarea");
    textarea.value = url;
    documentObject.body.appendChild(textarea);
    textarea.select();
    return documentObject.execCommand("copy") === true;
  } catch (_) {
    return false;
  } finally {
    try {
      if (typeof textarea?.remove === "function") textarea.remove();
      else textarea?.parentNode?.removeChild?.(textarea);
    } catch (_) {}
  }
}
