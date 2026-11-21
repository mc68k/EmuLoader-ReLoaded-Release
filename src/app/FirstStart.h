// FirstStart - the question a clean install asks when there's no EmuLoader in its own folder:
// whether there's one, or an earlier copy of this version, in a different folder.
#pragma once

namespace eld {

class Frontend;

// A clean install with no EmuLoader to bring data from in this folder: asked once whether
// there's one elsewhere. Its data is copied (Frontend::importFrom); MainWindow then says so
// and makes the games list. Skip starts fresh. Not asked again either way.
void askImportElsewhere(Frontend& fe);

} // namespace eld
