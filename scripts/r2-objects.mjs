import { createHash } from "node:crypto";
import { AwsClient } from "aws4fetch";

export const bucket = "esphub-firmware";
export const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");

export function r2Client() {
  return new AwsClient({
    accessKeyId: process.env.AWS_ACCESS_KEY_ID,
    secretAccessKey: process.env.AWS_SECRET_ACCESS_KEY,
    service: "s3",
    region: "auto",
  });
}

// Write an immutable object once and confirm the stored bytes match the build.
export async function publishObject(aws, endpoint, key, bytes, digest) {
  const type = key.endsWith(".json") ? "application/json" : "application/octet-stream";
  const url = new URL(bucket + "/" + key, endpoint);
  const put = await aws.fetch(url, {
    method: "PUT",
    headers: {
      "If-None-Match": "*",
      "Content-Type": type,
      "x-amz-meta-sha256": digest,
    },
    body: bytes,
  });
  if (!put.ok && put.status !== 412) {
    throw Error(`R2 upload failed (HTTP ${put.status}): ${key}`);
  }
  const stored = await aws.fetch(url, { method: "HEAD" });
  const length = stored.headers.get("Content-Length");
  if (!stored.ok || stored.headers.get("x-amz-meta-sha256") !== digest) {
    throw Error("R2 checksum metadata does not match the build: " + key);
  }
  if (length === null) {
    // R2 can omit Content-Length when the response is compressed.
    const object = await aws.fetch(url);
    const received = Buffer.from(await object.arrayBuffer());
    if (!object.ok || received.length !== bytes.length || sha256(received) !== digest) {
      throw Error("R2 object does not match the build: " + key);
    }
  } else if (Number(length) !== bytes.length) {
    throw Error("R2 object size does not match the build: " + key);
  }
}
