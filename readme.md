# ftag

ftag is a command line utility to tag files/directories on your filesystem, using inode numbers to track and identify them, without modifying files on disk

tags consist of a name, an optional color, and so-called supertags that they descend from

tag names can't have spaces, parens, square brackets, colons, and cannot start with a dash, encouraging a plain naming style `like-this`

with designating supertags, you can construct a large and complicated tag graph. ftag supports it fine and works with it, but placing a tag in a cycle with itself is discouraged for obvious reasons

the tag file format and index file format are designed to be almost entirely human-readable and editable
however, they do reference files by their inode numbers, so they might be slightly unwieldly to edit by hand

```
commands:
    search  : searches for and returns tags and files
    tag     : create/edit/delete tags, and assign and remove files from tags
    add     : adds files to be tracked/tagged by ftag
    rm      : removes files to be tracked/tagged by ftag
    update  : updates the index of tracked files, use if some have been moved/renamed
    fix     : fixes the inode numbers used in the tags file and file index
```

for more info, check out `ftag --help`
