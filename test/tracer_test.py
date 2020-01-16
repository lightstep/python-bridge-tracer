import tempfile
import os
import sys
import json
import unittest
import opentracing

for pyversion in os.listdir('binary'):
    sys.path.append('binary/' + pyversion)
import bridge_tracer

def make_mock_tracer(scope_manager = None):
    traces_path = os.path.join(tempfile.mkdtemp(prefix='python-bridge-test.'), 'traces.json')
    tracer = bridge_tracer.load_tracer(
            'external/io_opentracing_cpp/mocktracer/libmocktracer_plugin.so',
            '{ "output_file" : "%s" }' % traces_path)
    return tracer, traces_path

def read_spans(traces_path):
    with open(traces_path) as f:
        return json.loads(f.read())

class TestTracer(unittest.TestCase):
    def test_start_span(self):
        tracer, traces_path = make_mock_tracer()
        span = tracer.start_span('abc')
        span.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 1)
        self.assertEqual(spans[0]['operation_name'], 'abc')

    def test_start_span_none(self):
        tracer, traces_path = make_mock_tracer()
        span = tracer.start_span('abc', \
                                 child_of=None, \
                                 references=None, \
                                 tags=None)
        span.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 1)
        self.assertEqual(spans[0]['operation_name'], 'abc')

    def test_start_span_with_reference1(self):
        tracer, traces_path = make_mock_tracer()
        spanA = tracer.start_span('A')
        spanB = tracer.start_span('B', child_of=spanA)
        spanB.finish()
        spanA.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 2)
        traceIdA = spans[1]['span_context']['trace_id']
        spanIdA = spans[1]['span_context']['span_id']
        traceIdB = spans[0]['span_context']['trace_id']
        self.assertEqual(traceIdA, traceIdB)
        references = spans[0]['references']
        self.assertEqual(len(references), 1)
        self.assertEqual(references[0]['reference_type'], 'CHILD_OF')
        self.assertEqual(references[0]['trace_id'], traceIdA)
        self.assertEqual(references[0]['span_id'], spanIdA)

    def test_start_span_with_reference2(self):
        tracer, traces_path = make_mock_tracer()
        spanA = tracer.start_span('A')
        spanB = tracer.start_span('B', child_of=spanA.context)
        spanB.finish()
        spanA.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 2)
        traceIdA = spans[1]['span_context']['trace_id']
        spanIdA = spans[1]['span_context']['span_id']
        traceIdB = spans[0]['span_context']['trace_id']
        self.assertEqual(traceIdA, traceIdB)
        references = spans[0]['references']
        self.assertEqual(len(references), 1)
        self.assertEqual(references[0]['reference_type'], 'CHILD_OF')
        self.assertEqual(references[0]['trace_id'], traceIdA)
        self.assertEqual(references[0]['span_id'], spanIdA)

    def test_start_span_with_reference3(self):
        tracer, traces_path = make_mock_tracer()
        spanA = tracer.start_span('A')
        spanB = tracer.start_span('B', references=[
            opentracing.Reference(opentracing.ReferenceType.CHILD_OF, spanA.context)])
        spanB.finish()
        spanA.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 2)
        traceIdA = spans[1]['span_context']['trace_id']
        spanIdA = spans[1]['span_context']['span_id']
        traceIdB = spans[0]['span_context']['trace_id']
        self.assertEqual(traceIdA, traceIdB)
        references = spans[0]['references']
        self.assertEqual(len(references), 1)
        self.assertEqual(references[0]['reference_type'], 'CHILD_OF')
        self.assertEqual(references[0]['trace_id'], traceIdA)
        self.assertEqual(references[0]['span_id'], spanIdA)

    def test_start_span_with_reference4(self):
        tracer, traces_path = make_mock_tracer()
        spanA = tracer.start_active_span('A').span
        spanB = tracer.start_span('B')
        spanB.finish()
        spanA.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 2)
        traceIdA = spans[1]['span_context']['trace_id']
        spanIdA = spans[1]['span_context']['span_id']
        traceIdB = spans[0]['span_context']['trace_id']
        self.assertEqual(traceIdA, traceIdB)
        references = spans[0]['references']
        self.assertEqual(len(references), 1)
        self.assertEqual(references[0]['reference_type'], 'CHILD_OF')
        self.assertEqual(references[0]['trace_id'], traceIdA)
        self.assertEqual(references[0]['span_id'], spanIdA)

    def test_start_span_error(self):
        tracer, traces_path = make_mock_tracer()
        with self.assertRaises(TypeError):
            tracer.start_span(345)
        with self.assertRaises(Exception):
            tracer.start_span('abc', references=['duck'])
        with self.assertRaises(Exception):
            tracer.start_span('abc', tags=[])
        with self.assertRaises(Exception):
            tracer.start_span('abc', child_of='cat')

    def test_with1(self):
        tracer, traces_path = make_mock_tracer()
        with tracer.start_span('abc') as s:
            pass
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 1)
        self.assertTrue('error' not in spans[0]['tags'])

    def test_with1(self):
        tracer, traces_path = make_mock_tracer()
        try:
            with tracer.start_span('abc') as s:
                raise RuntimeError('crash n burn')
        except:
            pass
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 1)
        self.assertTrue(spans[0]['tags']['error'])

    def test_flush(self):
        tracer, traces_path = make_mock_tracer()
        tracer.flush(3.5)

    def test_set_operation_name(self):
        tracer, traces_path = make_mock_tracer()
        span = tracer.start_span('abc')
        span.set_operation_name('xyz')
        span.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 1)
        self.assertEqual(spans[0]['operation_name'], 'xyz')

    def test_set_tag1(self):
        tracer, traces_path = make_mock_tracer()
        span = tracer.start_span('abc')
        span.set_tag('a', 1)
        span.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 1)
        self.assertEqual(spans[0]['tags']['a'], 1)

    def test_set_tag2(self):
        tracer, traces_path = make_mock_tracer()
        span = tracer.start_span('abc', tags={'a':1})
        span.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 1)
        self.assertEqual(spans[0]['tags']['a'], 1)

    def test_log1(self):
        tracer, traces_path = make_mock_tracer()
        span = tracer.start_span('abc')
        span.log_kv({
            'cat': 'fluffy',
            'abc': 123
        })
        span.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 1)
        self.assertEqual(len(spans[0]['logs']), 1)
        fields = spans[0]['logs'][0]['fields']
        self.assertEqual(len(fields), 2)
        cat_field = [field for field in fields if field['key'] == 'cat']
        self.assertDictEqual(cat_field[0], {'key':'cat', 'value': 'fluffy'})
        abc_field = [field for field in fields if field['key'] == 'abc']
        self.assertDictEqual(abc_field[0], {'key':'abc', 'value': '123'})

    def test_log2(self):
        tracer, traces_path = make_mock_tracer()
        span = tracer.start_span('abc')
        span.log_event('rocket', payload='Boom')
        span.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 1)
        self.assertEqual(len(spans[0]['logs']), 1)
        fields = spans[0]['logs'][0]['fields']
        self.assertEqual(len(fields), 2)
        event_field = [field for field in fields if field['key'] == 'event']
        self.assertDictEqual(event_field[0], {'key':'event', 'value': 'rocket'})
        payload_field = [field for field in fields if field['key'] == 'payload']
        self.assertDictEqual(payload_field[0], {'key':'payload', 'value': 'Boom'})

    def test_log3(self):
        tracer, traces_path = make_mock_tracer()
        span = tracer.start_span('abc')
        span.log(event='rocket', payload='Boom')
        span.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 1)
        self.assertEqual(len(spans[0]['logs']), 1)
        fields = spans[0]['logs'][0]['fields']
        self.assertEqual(len(fields), 2)
        event_field = [field for field in fields if field['key'] == 'event']
        self.assertDictEqual(event_field[0], {'key':'event', 'value': 'rocket'})
        payload_field = [field for field in fields if field['key'] == 'payload']
        self.assertDictEqual(payload_field[0], {'key':'payload', 'value': 'Boom'})

    def test_baggage1(self):
        tracer, traces_path = make_mock_tracer()
        span = tracer.start_span('abc', tags={'a':1})
        span.set_baggage_item('abc', '123')
        self.assertEqual(span.get_baggage_item('abc'), '123')
        self.assertEqual(span.context.baggage, {'abc':'123'})
        span.set_baggage_item('xyz', '456')
        self.assertEqual(span.context.baggage, {'abc':'123', 'xyz':'456'})

    def test_get_tracer_from_span(self):
        tracer, traces_path = make_mock_tracer()
        span1 = tracer.start_span('abc')
        span2 = span1.tracer.start_span('123')
        span1.finish()
        span2.finish()
        tracer.close()
        spans = read_spans(traces_path)
        self.assertEqual(len(spans), 2)

    def test_propagation1(self):
        tracer, traces_path = make_mock_tracer()
        span1 = tracer.start_span('abc')
        carrier = {}
        tracer.inject(span1.context, opentracing.Format.TEXT_MAP, carrier)
        self.assertTrue(len(carrier) >= 1)
        span_context = tracer.extract(opentracing.Format.TEXT_MAP, carrier)
        self.assertIsNotNone(span_context)

    def test_propagation2(self):
        tracer, traces_path = make_mock_tracer()
        span1 = tracer.start_span('abc')
        carrier = bytearray()
        tracer.inject(span1.context, opentracing.Format.BINARY, carrier)
        self.assertTrue(len(carrier) >= 1)
        span_context = tracer.extract(opentracing.Format.BINARY, carrier)
        self.assertIsNotNone(span_context)

    def test_propagation3(self):
        tracer, _ = make_mock_tracer()
        carrier = {}
        span_context = tracer.extract(opentracing.Format.TEXT_MAP, carrier)
        self.assertIsNone(span_context)

    def test_propagation_error(self):
        tracer, traces_path = make_mock_tracer()
        carrier = {}
        with self.assertRaises(Exception):
            span_context = tracer.inject('bad-span', opentracing.Format.TEXT_MAP, carrier)
        with self.assertRaises(opentracing.UnsupportedFormatException):
            span_context = tracer.extract('no-such-format', carrier)
        with self.assertRaises(Exception):
            span_context = tracer.extract(opentracing.Format.TEXT_MAP, 'bad-carrier')
        with self.assertRaises(opentracing.InvalidCarrierException):
            span_context = tracer.extract(opentracing.Format.BINARY, {})

    def test_context(self):
        tracer, traces_path = make_mock_tracer()
        span = tracer.start_span('abc')
        print(span.context)

    def test_start_active_span(self):
        tracer, traces_path = make_mock_tracer()
        print(tracer.active_span)
        scope = tracer.start_active_span('abc')
        print(scope)
        print(tracer.active_span)

    def test_properties(self):
        tracer, traces_path = make_mock_tracer()
        print(tracer.scope_manager)

if __name__ == '__main__':
    unittest.main()
