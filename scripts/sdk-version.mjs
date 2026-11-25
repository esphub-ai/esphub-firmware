export const SDK_ROOT = "firmware/components/okou_agent/";
export const SDK_HEADER = SDK_ROOT + "include/okou_agent.h";

function versionParts(version) {
  if (
    typeof version !== "string" ||
    version.match(/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/)?.[0] !== version
  )
    throw Error(
      "Invalid SDK version: expected canonical numeric major.minor.patch",
    );
  const parts = version.split(".").map(Number);
  if (!parts.every(Number.isSafeInteger))
    throw Error("Invalid SDK version: components must be safe integers");
  return parts;
}

function sdkMacro(header) {
  if (typeof header !== "string") throw Error("SDK header must be a string");
  // Mask comments, preserving offsets and quoted text so replacement changes only
  // the version literal, not whitespace, comments, line endings or other macros.
  const source = header.replace(
    /"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'|\/\*[\s\S]*?\*\/|\/\/[^\r\n]*/g,
    (token) => (token.startsWith("/") ? token.replace(/[^\r\n]/g, " ") : token),
  );
  const macros = [
    ...source.matchAll(
      /^[\t ]*#[\t ]*define[\t ]+OKOU_SDK_VERSION\b[^\r\n]*/gm,
    ),
  ];
  if (macros.length !== 1)
    throw Error("SDK header must contain exactly one OKOU_SDK_VERSION macro");
  const macro = macros[0];
  const literal =
    /^([\t ]*#[\t ]*define[\t ]+OKOU_SDK_VERSION[\t ]+")([^"\r\n]*)("[\t ]*)$/.exec(
      macro[0],
    );
  if (!literal) throw Error("Invalid OKOU_SDK_VERSION macro");
  const version = literal[2];
  versionParts(version);
  return { version, start: macro.index + literal[1].length };
}

export function readSdkVersion(header) {
  return sdkMacro(header).version;
}

export function replaceSdkVersion(header, version) {
  versionParts(version);
  const current = sdkMacro(header);
  return (
    header.slice(0, current.start) +
    version +
    header.slice(current.start + current.version.length)
  );
}

function isSdkInput(path) {
  if (!path.startsWith(SDK_ROOT)) return false;
  const relative = path.slice(SDK_ROOT.length);
  return (
    !/\.(?:md|markdown|mdown|mdx)$/i.test(relative) &&
    !/(?:^|\/)(?:docs?|tests?|__tests__)(?:\/|$)/i.test(relative) &&
    !/(?:^|\/)(?:test|spec)(?:[_.-]|$)|(?:[_.-](?:test|spec))(?:\.|$)/i.test(
      relative,
    ) &&
    !/(?:^|\/)LICENSE(?:\.[^/]*)?$/i.test(relative)
  );
}

function sdkInputs(files, header, name) {
  if (!Array.isArray(files)) throw Error(`${name} must be a git-tree array`);
  const headers = files.filter((file) => file.path === SDK_HEADER);
  if (
    headers.length !== 1 ||
    headers[0].type !== "blob" ||
    !["100644", "100755"].includes(headers[0].mode)
  )
    throw Error(
      `${name} must contain one regular-file SDK header: ${SDK_HEADER}`,
    );
  const inputs = files
    .filter((file) => file.type !== "tree" && isSdkInput(file.path))
    .map((file) => [
      file.path,
      file.mode,
      file.type,
      file.path === SDK_HEADER ? replaceSdkVersion(header, "0.0.0") : file.sha,
    ])
    .sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0));
  return JSON.stringify(inputs);
}

export function sdkVersionPlan({
  baseHeader,
  headHeader,
  baseFiles,
  headFiles,
  labels = [],
}) {
  const minor = labels.includes("sdk:minor");
  const major = labels.includes("sdk:major");
  if (minor && major)
    throw Error("Conflicting SDK labels: sdk:minor and sdk:major");
  const baseVersion = readSdkVersion(baseHeader);
  const currentVersion = readSdkVersion(headHeader);
  const hasChanges =
    sdkInputs(baseFiles, baseHeader, "baseFiles") !==
    sdkInputs(headFiles, headHeader, "headFiles");
  const base = versionParts(baseVersion);
  const current = versionParts(currentVersion);
  let expectedVersion = currentVersion;
  if (hasChanges) {
    const index = major ? 0 : minor ? 1 : 2;
    const expected = base.map((part, i) =>
      i < index ? part : i === index ? part + 1 : 0,
    );
    expectedVersion = expected.join(".");
    versionParts(expectedVersion);
  } else {
    const index = current.findIndex((part, i) => part !== base[i]);
    if (index !== -1 && current[index] < base[index])
      throw Error(
        "SDK version must not decrease without meaningful SDK changes",
      );
  }
  return {
    hasChanges,
    baseVersion,
    currentVersion,
    expectedVersion,
    needsUpdate: currentVersion !== expectedVersion,
  };
}
