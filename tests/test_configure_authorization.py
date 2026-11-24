import json
import re
import sys
import unittest
from email.parser import HeaderParser
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))

import configure


class GatewayAuthorizationTest(unittest.TestCase):
    def test_pairing_declaration_sends_a_named_authorization_header(self):
        token = 'tokentokentokentokentokentoken12'
        header = configure.gateway_authorization_header(token)
        self.assertEqual(header, 'Authorization: Bearer ' + token)
        self.assertNotRegex(header, r'[\r\n]')

        declaration = configure.gateway_authorization_declaration(token + '\n')
        self.assertEqual(
            declaration,
            'constexpr char gatewayAuthorization[] = ' + json.dumps(header) + ';',
        )
        literal = re.search(r'gatewayAuthorization\[] = (".*");\Z', declaration).group(1)
        self.assertEqual(json.loads(literal), header)

        # WebSocketsClient::setExtraHeaders replaces the default Origin: file://
        # line, and sendHeader appends NEW_LINE after the extra headers.
        new_line = '\r\n'
        handshake = header + new_line + 'User-Agent: arduino-WebSocket-Client' + new_line + new_line
        parsed = HeaderParser().parsestr(handshake)
        self.assertEqual(parsed.get('Authorization'), 'Bearer ' + token)
        self.assertIsNone(parsed.get('Origin'))
