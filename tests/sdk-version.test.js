import assert from "node:assert/strict";
import test from "node:test";
import {
  SDK_ROOT,
  SDK_HEADER,
  readSdkVersion,
  replaceSdkVersion,
  sdkVersionPlan,
} from "../scripts/sdk-version.mjs";

const header = (version = "1.2.3") =>
  `#pragma once\n#define OKOU_SDK_VERSION "${version}"\nvoid okou_start(void);\n`;
const file = (path, sha = "base", mode = "100644", type = "blob") => ({
  path,
  mode,
  type,
  sha,
});
const sdkFile = (path, ...args) => file(SDK_ROOT + path, ...args);
const files = () => [
  file("README.md"),
  file("firmware/version.txt"),
  file("firmware/components", "components-tree", "040000", "tree"),
  file(SDK_ROOT.slice(0, -1), "sdk-tree", "040000", "tree"),
  file(SDK_HEADER, "header-base"),
  sdkFile("src/okou_agent.c"),
  sdkFile("CMakeLists.txt"),
  sdkFile("Kconfig"),
  sdkFile("idf_component.yml"),
];
const plan = (options = {}) =>
  sdkVersionPlan({
    baseHeader: header(),
    headHeader: header(),
    baseFiles: files(),
    headFiles: files(),
    ...options,
  });
const changedFiles = (path = SDK_ROOT + "src/okou_agent.c", changes = {}) =>
  files().map((entry) =>
    entry.path === path ? { ...entry, sha: "head", ...changes } : entry,
  );

test("exports the SDK component and header paths", () => {
  assert.equal(SDK_ROOT, "firmware/components/okou_agent/");
  assert.equal(SDK_HEADER, SDK_ROOT + "include/okou_agent.h");
});

for (const version of ["0.0.0", "1.2.3", "12.34.567", "9007199254740991.0.0"]) {
  test(`reads canonical SDK version ${version}`, () => {
    assert.equal(readSdkVersion(header(version)), version);
  });
}

test("reads whitespace, CRLF, trailing comments and an unterminated final line", () => {
  for (const suffix of [
    "",
    " // release",
    " /* release */",
    " /* a */ /* b */",
  ]) {
    assert.equal(
      readSdkVersion(`\t# define\tOKOU_SDK_VERSION\t"2.3.4"${suffix}\r\n`),
      "2.3.4",
    );
  }
  assert.equal(readSdkVersion('#define OKOU_SDK_VERSION "2.3.4"'), "2.3.4");
});

test("ignores macro examples in C comments and other quoted strings", () => {
  const source =
    '// #define OKOU_SDK_VERSION "8.8.8"\n' +
    '/*\n#define OKOU_SDK_VERSION "9.9.9"\n*/\n' +
    'const char *url = "https://example.org/* not a comment */";\n' +
    header();
  assert.equal(readSdkVersion(source), "1.2.3");
});

const invalidVersions = [
  "",
  "1",
  "1.2",
  "1.2.3.4",
  "01.2.3",
  "1.02.3",
  "1.2.03",
  "-1.2.3",
  "+1.2.3",
  "v1.2.3",
  "1.2.3-alpha",
  "1.2.3+build",
  " 1.2.3",
  "1.2.3 ",
  "1.2.3\n",
  "1.2.3\r\n",
  "1.2.3\t",
  "1.2.３",
  "9007199254740992.0.0",
  "0.9007199254740992.0",
  "0.0.9007199254740992",
  "0.0." + "9".repeat(400),
];
for (const version of invalidVersions) {
  test(`rejects invalid SDK version ${JSON.stringify(version)}`, () => {
    assert.throws(() => readSdkVersion(header(version)), /SDK|macro/);
    assert.throws(() => replaceSdkVersion(header(), version), /SDK version/);
  });
}

for (const macro of [
  "",
  '#define OTHER_VERSION "1.2.3"',
  '// #define OKOU_SDK_VERSION "1.2.3"',
  '/* #define OKOU_SDK_VERSION "1.2.3" */',
  '#define OKOU_SDK_VERSION_EXTRA "1.2.3"',
  "#define OKOU_SDK_VERSION",
  "#define OKOU_SDK_VERSION 1.2.3",
  '#define OKOU_SDK_VERSION ("1.2.3")',
  '#define OKOU_SDK_VERSION(x) "1.2.3"',
  '#define OKOU_SDK_VERSION "1.2.3" extra',
  '#define OKOU_SDK_VERSION "1.2.3" "4.5.6"',
  '#define OKOU_SDK_VERSION "1.2.3"; ',
  '#define OKOU_SDK_VERSION "1.2.3',
  '#define OKOU_SDK_VERSION \\\n"1.2.3"',
  '#define OKOU_SDK_VERSION "1.2.3" \\\n+ 1',
  '#define OKOU_SDK_VERSION "1.2.3"\n#define OKOU_SDK_VERSION "1.2.3"',
  '#define OKOU_SDK_VERSION "1.2.3"\n#define OKOU_SDK_VERSION invalid',
  '#if A\n#define OKOU_SDK_VERSION "1.2.3"\n#else\n#define OKOU_SDK_VERSION "1.2.4"\n#endif',
]) {
  test(`rejects missing, malformed or multiple macros: ${JSON.stringify(macro)}`, () => {
    assert.throws(() => readSdkVersion(macro), /SDK|macro/);
    assert.throws(() => replaceSdkVersion(macro, "2.0.0"), /SDK|macro/);
  });
}

test("rejects non-string headers and replacement versions", () => {
  for (const value of [undefined, null, 123, {}, ["1.2.3"]]) {
    assert.throws(() => readSdkVersion(value), /header must be a string/);
    assert.throws(() => replaceSdkVersion(header(), value), /SDK version/);
  }
});

test("replacement preserves every byte except the version literal", () => {
  const source =
    '/* 1.2.3 */\r\n\t# define /* SDK */ OKOU_SDK_VERSION\t"1.2.3" /* 1.2.3 */\r\n' +
    '#define OTHER_VERSION "1.2.3"\r\n';
  const expected = source.replace('VERSION\t"1.2.3"', 'VERSION\t"20.30.40"');
  assert.equal(replaceSdkVersion(source, "20.30.40"), expected);
  assert.equal(readSdkVersion(expected), "20.30.40");
  assert.equal(replaceSdkVersion(expected, "20.30.40"), expected);
});

test("unchanged SDK returns the full no-op result", () => {
  assert.deepEqual(plan(), {
    hasChanges: false,
    baseVersion: "1.2.3",
    currentVersion: "1.2.3",
    expectedVersion: "1.2.3",
    needsUpdate: false,
  });
});

for (const path of [
  "src/okou_agent.c",
  "src/okou_priv.h",
  "include/okou_agent_ext.h",
  "CMakeLists.txt",
  "Kconfig",
  "idf_component.yml",
]) {
  test(`SDK source/header/build input ${path} requires a patch`, () => {
    const target = SDK_ROOT + path;
    const baseFiles = [...files(), file(target)].filter(
      (entry, i, all) =>
        all.findIndex((other) => other.path === entry.path) === i,
    );
    const headFiles = baseFiles.map((entry) =>
      entry.path === target ? { ...entry, sha: "changed" } : entry,
    );
    assert.deepEqual(plan({ baseFiles, headFiles }), {
      hasChanges: true,
      baseVersion: "1.2.3",
      currentVersion: "1.2.3",
      expectedVersion: "1.2.4",
      needsUpdate: true,
    });
  });
}

test("public SDK header content is compared rather than its git blob SHA", () => {
  assert.equal(plan({ headFiles: changedFiles(SDK_HEADER) }).hasChanges, false);
  assert.equal(
    plan({ headHeader: header() + "void okou_stop(void);\n" }).hasChanges,
    true,
  );
  assert.equal(
    plan({ headFiles: changedFiles(SDK_HEADER, { mode: "100755" }) })
      .hasChanges,
    true,
  );
});

test("header whitespace, comments and line-ending edits are meaningful", () => {
  for (const headHeader of [
    header().replace("#define", "# define"),
    header().replace('"1.2.3"', '"1.2.3" // release'),
    header().replaceAll("\n", "\r\n"),
    header().replace("void okou_start", "void okou_begin"),
  ]) {
    assert.equal(plan({ headHeader }).hasChanges, true);
  }
});

test("SDK additions, deletions, renames, symlinks and file mode changes count", () => {
  const source = SDK_ROOT + "src/okou_agent.c";
  for (const headFiles of [
    [...files(), sdkFile("src/new.c")],
    files().filter((entry) => entry.path !== source),
    files().map((entry) =>
      entry.path === source
        ? { ...entry, path: SDK_ROOT + "src/renamed.c" }
        : entry,
    ),
    changedFiles(source, { sha: "base", mode: "100755" }),
    changedFiles(source, { sha: "base", mode: "120000" }),
    [...files(), sdkFile("vendor/lib", "revision", "160000", "commit")],
  ]) {
    assert.equal(plan({ headFiles }).hasChanges, true);
  }
});

const excludedPaths = [
  SDK_ROOT + "README.md",
  SDK_ROOT + "guide.MD",
  SDK_ROOT + "guide.markdown",
  SDK_ROOT + "guide.mdx",
  SDK_ROOT + "docs/api.rst",
  SDK_ROOT + "doc/guide.txt",
  SDK_ROOT + "src/docs/example.c",
  SDK_ROOT + "test/test_agent.c",
  SDK_ROOT + "tests/CMakeLists.txt",
  SDK_ROOT + "__tests__/agent.js",
  SDK_ROOT + "test_apps/smoke/main.c",
  SDK_ROOT + "src/test_agent.c",
  SDK_ROOT + "src/agent_test.c",
  SDK_ROOT + "src/agent.test.js",
  SDK_ROOT + "src/agent.spec.js",
  SDK_ROOT + "LICENSE",
  SDK_ROOT + "LICENSE.txt",
  SDK_ROOT + "vendor/LICENSE.MIT",
  "firmware/components/okou_agent_extra/src/agent.c",
  "firmware/components/other/src/agent.c",
  "firmware/version.txt",
  "scripts/build-bootloader.sh",
  "services/web/src/app.js",
  "tests/sdk-version.test.js",
];
for (const path of excludedPaths) {
  test(`ignores excluded path ${path} on add, update and delete`, () => {
    const baseFiles = files().filter((entry) => entry.path !== path);
    const withFile = [...baseFiles, file(path)];
    for (const [before, after] of [
      [baseFiles, withFile],
      [withFile, [...baseFiles, file(path, "head", "100755")]],
      [withFile, baseFiles],
    ]) {
      assert.equal(
        plan({ baseFiles: before, headFiles: after }).hasChanges,
        false,
      );
    }
  });
}

test("does not exclude source names that merely contain docs or test", () => {
  for (const path of ["src/testable.c", "src/docs_api.c", "src/latest.c"]) {
    assert.equal(
      plan({ headFiles: [...files(), sdkFile(path)] }).hasChanges,
      true,
    );
  }
});

test("recursive directory tree SHA changes do not bypass input exclusions", () => {
  const headFiles = files().map((entry) =>
    entry.type === "tree" ? { ...entry, sha: "changed-tree" } : entry,
  );
  headFiles.push(sdkFile("docs", "docs-tree", "040000", "tree"));
  assert.equal(plan({ headFiles }).hasChanges, false);
});

for (const headVersion of ["1.2.4", "1.10.0", "2.0.0", "20.30.40"]) {
  test(`preserves a manual version-only bump to ${headVersion}`, () => {
    assert.deepEqual(
      plan({
        headHeader: header(headVersion),
        headFiles: changedFiles(SDK_HEADER),
      }),
      {
        hasChanges: false,
        baseVersion: "1.2.3",
        currentVersion: headVersion,
        expectedVersion: headVersion,
        needsUpdate: false,
      },
    );
  });
}

test("version-only changes and excluded paths do not become label-driven bumps", () => {
  assert.equal(
    plan({
      headHeader: header("2.0.0"),
      headFiles: [...changedFiles(SDK_HEADER), sdkFile("docs/guide.md")],
      labels: ["sdk:major"],
    }).expectedVersion,
    "2.0.0",
  );
  assert.equal(plan({ labels: ["sdk:minor"] }).expectedVersion, "1.2.3");
});

test("rejects version-only decreases using numeric rather than lexical ordering", () => {
  for (const [baseVersion, headVersion] of [
    ["1.2.3", "1.2.2"],
    ["1.2.3", "1.1.99"],
    ["1.2.3", "0.99.99"],
    ["10.0.0", "2.99.99"],
    ["1.10.0", "1.2.99"],
    ["1.2.10", "1.2.9"],
  ]) {
    assert.throws(
      () =>
        plan({
          baseHeader: header(baseVersion),
          headHeader: header(headVersion),
        }),
      /must not decrease/,
    );
  }
  assert.equal(
    plan({ baseHeader: header("2.99.99"), headHeader: header("10.0.0") })
      .needsUpdate,
    false,
  );
});

for (const [labels, expectedVersion] of [
  [[], "1.9.100"],
  [["sdk:minor"], "1.10.0"],
  [["sdk:major"], "2.0.0"],
  [["sdk:minor", "sdk:minor"], "1.10.0"],
  [["sdk:patch", "bug", "sdk:MAJOR", "sdk:major-extra"], "1.9.100"],
]) {
  test(`computes exact base increment and resets for ${JSON.stringify(labels)}`, () => {
    const options = {
      baseHeader: header("1.9.99"),
      headHeader: header("42.42.42"),
      headFiles: changedFiles(),
      labels,
    };
    const result = plan(options);
    assert.equal(result.expectedVersion, expectedVersion);
    assert.equal(result.needsUpdate, true);
    const headHeader = replaceSdkVersion(options.headHeader, expectedVersion);
    assert.deepEqual(plan({ ...options, headHeader }), {
      ...result,
      currentVersion: expectedVersion,
      needsUpdate: false,
    });
  });
}

test("a stale lower head version is repaired to the exact base bump on SDK changes", () => {
  const result = plan({
    headHeader: header("0.0.1"),
    headFiles: changedFiles(),
  });
  assert.equal(result.expectedVersion, "1.2.4");
  assert.equal(result.needsUpdate, true);
});

test("conflicting labels throw even on no-op changes; unknown labels are ignored", () => {
  for (const headFiles of [files(), changedFiles()]) {
    assert.throws(
      () => plan({ headFiles, labels: ["sdk:minor", "sdk:major"] }),
      /Conflicting SDK labels/,
    );
  }
  assert.deepEqual(plan({ labels: ["bug", "sdk:patch"] }), plan());
});

test("guards safe-integer overflow while allowing resets of large components", () => {
  const max = Number.MAX_SAFE_INTEGER;
  for (const [baseVersion, labels] of [
    [`1.2.${max}`, []],
    [`1.${max}.0`, ["sdk:minor"]],
    [`${max}.0.0`, ["sdk:major"]],
  ]) {
    assert.throws(
      () =>
        plan({
          baseHeader: header(baseVersion),
          headFiles: changedFiles(),
          labels,
        }),
      /safe integers/,
    );
  }
  for (const [version, labels, expected] of [
    [`1.${max}.${max}`, ["sdk:major"], "2.0.0"],
    [`1.2.${max}`, ["sdk:minor"], "1.3.0"],
    [`${max}.0.0`, [], `${max}.0.1`],
  ]) {
    assert.equal(
      plan({ baseHeader: header(version), headFiles: changedFiles(), labels })
        .expectedVersion,
      expected,
    );
  }
  assert.equal(
    plan({ baseHeader: header(`${max}.0.0`), headHeader: header(`${max}.0.0`) })
      .needsUpdate,
    false,
  );
});

for (const field of ["baseFiles", "headFiles"]) {
  test(`requires the header as exactly one regular file in ${field}`, () => {
    const withoutHeader = files().filter((entry) => entry.path !== SDK_HEADER);
    for (const entries of [
      withoutHeader,
      [...files(), file(SDK_HEADER)],
      [...withoutHeader, file(SDK_HEADER, "link", "120000", "blob")],
      [...withoutHeader, file(SDK_HEADER, "tree", "040000", "tree")],
      [...withoutHeader, file(SDK_HEADER, "commit", "160000", "commit")],
      [...withoutHeader, file(SDK_HEADER, "invalid", "100644", "commit")],
      [...withoutHeader, file(SDK_HEADER, "invalid", "100600", "blob")],
    ]) {
      assert.throws(
        () => plan({ [field]: entries }),
        /regular-file SDK header/,
      );
    }
    assert.throws(() => plan({ [field]: undefined }), /git-tree array/);
    assert.throws(() => plan({ [field]: {} }), /git-tree array/);
  });
}

test("validates both header contents even without changes", () => {
  for (const field of ["baseHeader", "headHeader"]) {
    assert.throws(() => plan({ [field]: "" }), /exactly one/);
    assert.throws(() => plan({ [field]: header("01.2.3") }), /SDK version/);
  }
});

test("tree order is irrelevant and inputs are not mutated", () => {
  const baseFiles = Object.freeze(files().map(Object.freeze));
  const headFiles = Object.freeze([...baseFiles].reverse());
  const labels = Object.freeze(["sdk:minor"]);
  const options = Object.freeze({
    baseHeader: header(),
    headHeader: header(),
    baseFiles,
    headFiles,
    labels,
  });
  assert.deepEqual(sdkVersionPlan(options), plan());
  assert.deepEqual(sdkVersionPlan(options), sdkVersionPlan(options));
});
