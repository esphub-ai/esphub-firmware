export const SDK_ROOT = "firmware/components/eh_agent/";
export const SDK_HEADER = SDK_ROOT + "include/eh_agent.h";

export function sdkHeaderEntry(files) {
  if (!Array.isArray(files)) throw Error("Expected a git-tree array");
  const entries = files.filter(file => file.path === SDK_HEADER);
  if (entries.length !== 1 || entries[0].type !== "blob" ||
      !["100644", "100755"].includes(entries[0].mode))
    throw Error("Expected one regular-file SDK header");
  return entries[0];
}

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
      /^[\t ]*#[\t ]*define[\t ]+EH_SDK_VERSION\b[^\r\n]*/gm,
    ),
  ];
  if (macros.length !== 1)
    throw Error("SDK header must contain exactly one SDK version macro");
  const macro = macros[0];
  const literal =
    /^([\t ]*#[\t ]*define[\t ]+EH_SDK_VERSION[\t ]+")([^"\r\n]*)("[\t ]*)$/.exec(
      macro[0],
    );
  if (!literal) throw Error("Invalid SDK version macro");
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

function isSdkInput(path, root) {
  if (!path.startsWith(root)) return false;
  const relative = path.slice(root.length);
  return (
    !/\.(?:md|markdown|mdown|mdx)$/i.test(relative) &&
    !/(?:^|\/)(?:docs?|tests?|__tests__)(?:\/|$)/i.test(relative) &&
    !/(?:^|\/)(?:test|spec)(?:[_.-]|$)|(?:[_.-](?:test|spec))(?:\.|$)/i.test(
      relative,
    ) &&
    !/(?:^|\/)LICENSE(?:\.[^/]*)?$/i.test(relative)
  );
}

function sdkInputs(files, header) {
  const { path } = sdkHeaderEntry(files);
  const root = path.slice(0, path.lastIndexOf("include/"));
  const inputs = files
    .filter((file) => file.type !== "tree" && isSdkInput(file.path, root))
    .map((file) => [
      file.path,
      file.mode,
      file.type,
      file.path === path ? replaceSdkVersion(header, "0.0.0") : file.sha,
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
    sdkInputs(baseFiles, baseHeader) !==
    sdkInputs(headFiles, headHeader);
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
