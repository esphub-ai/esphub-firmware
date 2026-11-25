const segment = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;

export function boardId(group, project) {
  if (!segment.test(group) || !segment.test(project))
    throw Error(`Invalid board directory: ${group}/${project}`);
  return `${group}/${project}`;
}

// Board IDs cannot contain underscores, so this preserves both parts in a filename.
export function boardFileStem(board) {
  return board.replace("/", "_");
}
