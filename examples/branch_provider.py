#!/usr/bin/env python3
"""Offline example of the optional branch-advice protocol.

A model adapter can implement this same protocol. Return one currently active
variable/value, echoing the request id; the solver retains all alternatives.
This example chooses the last value of the solver's suggested variable.
"""
import json
import sys
request = json.load(sys.stdin)
variable = next(v for v in request['variables'] if v['id'] == request['fallback'])
print(json.dumps({'id': request['id'], 'variable': variable['id'],
                  'value': variable['domain'][-1]}))
