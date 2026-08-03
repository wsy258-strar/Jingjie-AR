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
  try {
    await navigatorObject?.clipboard?.writeText(url);
    return true;
  } catch (_) {}

  if (!documentObject?.createElement || !documentObject.body?.appendChild || !documentObject.execCommand) {
    return false;
  }

  const textarea = documentObject.createElement("textarea");
  textarea.value = url;
  documentObject.body.appendChild(textarea);
  try {
    textarea.select();
    return documentObject.execCommand("copy") === true;
  } catch (_) {
    return false;
  } finally {
    textarea.remove?.();
  }
}
