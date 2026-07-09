#ifndef PALA_WEB_FILES_H
#define PALA_WEB_FILES_H

// Mounts the home page (`/`) and the file/folder browser (`/files`) plus the
// mutating endpoints they use: /del, /mkdir, /rmdir, /move, /jumppage.
void registerFilesRoutes();

#endif  // PALA_WEB_FILES_H
