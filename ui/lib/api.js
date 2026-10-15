// The REST client (ARCHITECTURE.md section 13). Every request that changes state sends a JSON or
// binary content type, which the box requires.

export class ApiError extends Error {
  constructor(status, code, message, details = []) {
    super(message);
    this.status = status;
    this.code = code;
    this.details = details;
  }
}

export function createApi(fetchImpl = globalThis.fetch?.bind(globalThis)) {
  async function request(method, path, body) {
    const init = { method, credentials: "same-origin", headers: { Accept: "application/json" } };
    if (method !== "GET") {
      init.headers["Content-Type"] = "application/json";
      init.body = JSON.stringify(body ?? {});
    }
    let response;
    try {
      response = await fetchImpl(path, init);
    } catch {
      throw new ApiError(0, "network", "");
    }
    let data = null;
    const text = await response.text();
    if (text) {
      try {
        data = JSON.parse(text);
      } catch {
        data = null;
      }
    }
    if (!response.ok) {
      const error = (data && data.error) || {};
      throw new ApiError(
        response.status,
        error.code || `http_${response.status}`,
        error.message || "",
        error.details || []
      );
    }
    return data;
  }

  return {
    get: (path) => request("GET", path),
    post: (path, body) => request("POST", path, body),
    patch: (path, body) => request("PATCH", path, body),
    del: (path) => request("DELETE", path),
  };
}

// Uploads a file as the raw request body, reporting progress from 0 to 100. Uses XMLHttpRequest
// because fetch cannot report upload progress.
export function upload(path, file, onProgress) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open("POST", path);
    xhr.setRequestHeader("Content-Type", "application/octet-stream");
    xhr.upload.onprogress = (event) => {
      if (event.lengthComputable) onProgress(Math.round((event.loaded / event.total) * 100));
    };
    xhr.onload = () => {
      let data = null;
      try {
        data = JSON.parse(xhr.responseText);
      } catch {
        data = null;
      }
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(data);
      } else {
        const error = (data && data.error) || {};
        reject(new ApiError(xhr.status, error.code || "upload_failed", error.message || ""));
      }
    };
    xhr.onerror = () => reject(new ApiError(0, "network", ""));
    xhr.send(file);
  });
}
