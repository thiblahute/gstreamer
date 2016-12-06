#include <gst/check/gstcheck.h>

gint object_repr_level = 0;
extern gchar *_priv_gst_object_get_repr (GstObject * self);

#define CHECK_REPR(obj, expected) G_STMT_START {                                    \
    gchar *repr = _priv_gst_object_get_repr ((GstObject*) (obj)); \
    assert_equals_string (repr, expected); \
    g_free (repr); \
} G_STMT_END;

GST_START_TEST (test_object_path_repr)
{
  GstPad *pad;
  GstElement *pipeline;
  GstElement *bin, *bin1;

  object_repr_level = 2;

  pipeline = gst_pipeline_new ("p");
  CHECK_REPR (pipeline, "</p>");

  bin = gst_bin_new ("b");
  CHECK_REPR (bin, "</b>");

  gst_bin_add (GST_BIN (pipeline), bin);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</p/b>");

  gst_object_ref (bin);
  g_assert (gst_bin_remove (GST_BIN (pipeline), bin));
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</b>");

  bin1 = gst_bin_new ("b1");
  gst_bin_add (GST_BIN (pipeline), bin);
  gst_bin_add (GST_BIN (bin), bin1);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</p/b>");
  CHECK_REPR (bin1, "</p/b/b1>");

  g_assert (gst_bin_remove (GST_BIN (pipeline), bin));
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</b>");
  CHECK_REPR (bin1, "</b/b1>");

  pad = gst_pad_new ("pad", GST_PAD_SRC);
  g_assert (gst_element_add_pad (bin1, pad));
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</b>");
  CHECK_REPR (bin1, "</b/b1>");
  CHECK_REPR (pad, "</b/b1:pad>");

  gst_bin_add (GST_BIN (pipeline), bin);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</p/b>");
  CHECK_REPR (bin1, "</p/b/b1>");
  CHECK_REPR (pad, "</p/b/b1:pad>");

  gst_object_ref (bin1);
  gst_bin_remove (GST_BIN (bin), bin1);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</p/b>");
  CHECK_REPR (bin1, "</b1>");
  CHECK_REPR (pad, "</b1:pad>");

  gst_object_ref (pad);
  gst_element_remove_pad (bin1, pad);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</p/b>");
  CHECK_REPR (bin1, "</b1>");
  CHECK_REPR (pad, "<(NULL):pad>");

  gst_element_add_pad (bin1, pad);
  gst_bin_add (GST_BIN (bin), bin1);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</p/b>");
  CHECK_REPR (bin1, "</p/b/b1>");
  CHECK_REPR (pad, "</p/b/b1:pad>");

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_object_toplevel_repr)
{
  GstPad *pad;
  GstElement *pipeline;
  GstElement *bin, *bin1;

  object_repr_level = 1;

  pipeline = gst_pipeline_new ("p");
  CHECK_REPR (pipeline, "</p>");

  bin = gst_bin_new ("b");
  CHECK_REPR (bin, "</b>");

  gst_bin_add (GST_BIN (pipeline), bin);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "<p/../b>");

  gst_object_ref (bin);
  g_assert (gst_bin_remove (GST_BIN (pipeline), bin));
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</b>");

  bin1 = gst_bin_new ("b1");
  gst_bin_add (GST_BIN (pipeline), bin);
  gst_bin_add (GST_BIN (bin), bin1);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "<p/../b>");
  CHECK_REPR (bin1, "<p/../b1>");

  g_assert (gst_bin_remove (GST_BIN (pipeline), bin));
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</b>");
  CHECK_REPR (bin1, "<b/../b1>");

  pad = gst_pad_new ("pad", GST_PAD_SRC);
  g_assert (gst_element_add_pad (bin1, pad));
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "</b>");
  CHECK_REPR (bin1, "<b/../b1>");
  CHECK_REPR (pad, "<b/../b1:pad>");

  gst_bin_add (GST_BIN (pipeline), bin);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "<p/../b>");
  CHECK_REPR (bin1, "<p/../b1>");
  CHECK_REPR (pad, "<p/../b1:pad>");

  gst_object_ref (bin1);
  gst_bin_remove (GST_BIN (bin), bin1);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "<p/../b>");
  CHECK_REPR (bin1, "</b1>");
  CHECK_REPR (pad, "<b1:pad>");

  gst_object_ref (pad);
  gst_element_remove_pad (bin1, pad);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "<p/../b>");
  CHECK_REPR (bin1, "</b1>");
  CHECK_REPR (pad, "<(NULL):pad>");

  gst_element_add_pad (bin1, pad);
  gst_bin_add (GST_BIN (bin), bin1);
  CHECK_REPR (pipeline, "</p>");
  CHECK_REPR (bin, "<p/../b>");
  CHECK_REPR (bin1, "<p/../b1>");
  CHECK_REPR (pad, "<p/../b1:pad>");

  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
gst_suite (void)
{
  Suite *s = suite_create ("Gst");
  TCase *tc_chain = tcase_create ("gst tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_object_path_repr);
  tcase_add_test (tc_chain, test_object_toplevel_repr);

  return s;
}

GST_CHECK_MAIN (gst);
