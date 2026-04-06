#include "html-source.h"

#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QPainter>

void render_qt(struct html_source_data *hs, obs_data_t *settings) {

	const char *new_html = obs_data_get_string(settings, "text");
	if (hs->html.array && strcmp(hs->html.array, new_html) == 0)
		return;
	dstr_copy(&hs->html, new_html);

	QTextDocument td;
	td.setHtml(QString::fromUtf8(hs->html.array));
	auto size = td.documentLayout()->documentSize();
	if (!hs->texture || size != QSizeF(hs->cx, hs->cy)) {
		obs_enter_graphics();
		hs->cx = (uint32_t)size.width();
		hs->cy = (uint32_t)size.height();
		if (hs->texture)
			gs_texture_destroy(hs->texture);
		hs->texture = gs_texture_create(hs->cx, hs->cy, GS_RGBA, 1, NULL, GS_DYNAMIC);
		obs_leave_graphics();
	}
	QImage image(hs->cx, hs->cy, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	td.drawContents(&painter);
	painter.end();
	obs_enter_graphics();
	gs_texture_set_image(hs->texture, image.constBits(), image.bytesPerLine(), false);
	obs_leave_graphics();
}
