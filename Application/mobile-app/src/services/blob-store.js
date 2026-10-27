// H5 file storage. Explicit transfers read these bytes from IndexedDB.
function openDB() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open('v821-companion-files', 1)
    request.onupgradeneeded = () => request.result.createObjectStore('blobs')
    request.onsuccess = () => resolve(request.result)
    request.onerror = () => reject(new Error('浏览器无法打开本地文件存储'))
  })
}
export async function blobAction(action, key, value) {
  const db = await openDB()
  try {
    return await new Promise((resolve, reject) => {
      const tx = db.transaction('blobs', action === 'get' ? 'readonly' : 'readwrite')
      const store = tx.objectStore('blobs')
      const request =
        action === 'put'
          ? store.put(value, key)
          : action === 'clear'
            ? store.clear()
            : action === 'delete'
              ? store.delete(key)
              : store.get(key)
      tx.oncomplete = () => resolve(request.result)
      tx.onerror = tx.onabort = () => reject(new Error('本地文件存储失败，请检查浏览器空间'))
    })
  } finally {
    db.close()
  }
}
export async function exportFile(file) {
  const blob = file.blobId ? await blobAction('get', file.blobId) : null
  if (!blob) throw new Error('本地文件内容已被浏览器清理，请重新导入')
  return exportBlob(blob, file.name)
}
export function exportBlob(blob, name) {
  const url = URL.createObjectURL(blob)
  const anchor = document.createElement('a')
  anchor.href = url
  anchor.download = name
  anchor.style.display = 'none'
  document.body.appendChild(anchor)
  anchor.click()
  anchor.remove()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}
