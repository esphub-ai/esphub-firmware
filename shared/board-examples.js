import { repository } from "./repository.js";

export function describeExamples(board, examples, revision) {
  const tree = repository + "/tree/" + revision + "/firmware/board/" + board;
  const blob = repository + "/blob/" + revision;
  return examples.map(({ id, entrypoint, description, upstream }) => ({
    id, description, upstream,
    source_url: tree + "/examples/" + id,
    entrypoint: entrypoint.slice(("examples/" + id + "/").length),
    build_guide_url: blob + "/docs/build-examples.md",
    build_command: "python3 scripts/build-example.py " + board + " --example " + id,
  }));
}
