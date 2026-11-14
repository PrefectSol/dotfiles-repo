const sheet = "data:text/css,html:root{background-color:light-dark(%23ffffff,%2315161e)}";
const done = new WeakSet();

function clear(win, el) {
	if (!el)
		return true;
	const s = win.getComputedStyle(el);
	return s.backgroundImage == "none" && /^(transparent|rgba\(.*, 0\))$/.test(s.backgroundColor);
}

function check(e) {
	const doc = e.target, win = content;
	if (doc != win.document || !/^(https?|file|view-source):/.test(doc.documentURI) || doc.documentURI.startsWith("file:///usr/share/distro/"))
		return;
	const utils = win.windowUtils;
	if (done.has(doc)) {
		utils.removeSheetUsingURIString(sheet, utils.USER_SHEET);
		done.delete(doc);
	}
	if (clear(win, doc.documentElement) && clear(win, doc.body)) {
		utils.loadSheetUsingURIString(sheet, utils.USER_SHEET);
		done.add(doc);
	}
}

addEventListener("DOMContentLoaded", check, true);
addEventListener("pageshow", check, true);
