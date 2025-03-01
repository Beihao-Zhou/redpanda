# Copyright 2020 Redpanda Data, Inc.
#
# Use of this software is governed by the Business Source License
# included in the file licenses/BSL.md
#
# As of the Change Date specified in that file, in accordance with
# the Business Source License, use of this software will be governed
# by the Apache License, Version 2.0
from enum import IntEnum
from typing import List
import json
import random
import socket
import string
import requests
from requests.exceptions import HTTPError
import socket
import time
import urllib.parse
import re

from confluent_kafka import KafkaException, KafkaError

from ducktape.cluster.cluster import ClusterNode
from ducktape.mark import parametrize, matrix
from ducktape.utils.util import wait_until
from ducktape.errors import TimeoutError
from ducktape.services.service import Service

from rptest.services.cluster import cluster
from rptest.tests.redpanda_test import RedpandaTest
from rptest.clients.kcl import RawKCL
from rptest.clients.kafka_cli_tools import KafkaCliTools, KafkaCliToolsError
from rptest.clients.types import TopicSpec
from rptest.clients.rpk import RpkTool, RpkException
from rptest.clients.python_librdkafka import PythonLibrdkafka
from rptest.services.admin import Admin
from rptest.services.redpanda import SecurityConfig, SaslCredentials, SecurityConfig, TLSProvider
from rptest.services.tls import Certificate, CertificateAuthority, TLSCertManager
from rptest.tests.sasl_reauth_test import get_sasl_metrics, REAUTH_METRIC, EXPIRATION_METRIC
from rptest.util import expect_http_error
from rptest.utils.log_utils import wait_until_nag_is_set
from rptest.utils.utf8 import CONTROL_CHARS, CONTROL_CHARS_MAP, generate_string_with_control_character


class BaseScramTest(RedpandaTest):
    def __init__(self, test_context, **kwargs):
        super(BaseScramTest, self).__init__(test_context, **kwargs)

    def update_user(self, username, quote: bool = True):
        def gen(length):
            return "".join(
                random.choice(string.ascii_letters) for _ in range(length))

        if quote:
            username = urllib.parse.quote(username, safe='')
        password = gen(20)

        controller = self.redpanda.nodes[0]
        url = f"http://{controller.account.hostname}:9644/v1/security/users/{username}"
        data = dict(
            username=username,
            password=password,
            algorithm="SCRAM-SHA-256",
        )
        res = requests.put(url, json=data)

        assert res.status_code == 200
        return password

    def delete_user(self, username, quote: bool = True):
        if quote:
            username = urllib.parse.quote(username, safe='')
        controller = self.redpanda.nodes[0]
        url = f"http://{controller.account.hostname}:9644/v1/security/users/{username}"
        res = requests.delete(url)
        assert res.status_code == 200, f"Status code: {res.status_code} for DELETE user {username}"

    def list_users(self):
        controller = self.redpanda.nodes[0]
        url = f"http://{controller.account.hostname}:9644/v1/security/users"
        res = requests.get(url)
        assert res.status_code == 200
        return res.json()

    def create_user(self,
                    username,
                    algorithm,
                    password=None,
                    expected_status_code=200,
                    err_msg=None):
        def gen(length):
            return "".join(
                random.choice(string.ascii_letters) for _ in range(length))

        if password is None:
            password = gen(15)

        controller = self.redpanda.nodes[0]
        url = f"http://{controller.account.hostname}:9644/v1/security/users"
        data = dict(
            username=username,
            password=password,
            algorithm=algorithm,
        )
        self.logger.debug(f"User Creation Arguments: {data}")
        res = requests.post(url, json=data)

        assert res.status_code == expected_status_code, f"Expected {expected_status_code}, got {res.status_code}: {res.content}"

        if err_msg is not None:
            assert res.json(
            )['message'] == err_msg, f"{res.json()['message']} != {err_msg}"

        return password

    def make_superuser_client(self,
                              password_override=None,
                              algorithm_override=None):
        username, password, algorithm = self.redpanda.SUPERUSER_CREDENTIALS
        password = password_override or password
        algorithm = algorithm_override or algorithm
        return PythonLibrdkafka(self.redpanda,
                                username=username,
                                password=password,
                                algorithm=algorithm)


class ScramTest(BaseScramTest):
    def __init__(self, test_context):
        security = SecurityConfig()
        security.enable_sasl = True
        super(ScramTest,
              self).__init__(test_context,
                             num_brokers=3,
                             security=security,
                             extra_node_conf={'developer_mode': True})

    @cluster(num_nodes=3)
    @parametrize(alternate_listener=False)
    @parametrize(alternate_listener=True)
    def test_redirects(self, alternate_listener):
        """
        This test is for Admin API redirection functionality in general, but is in this
        test class because managing SCRAM/SASL users is one of the key areas that relies
        on redirection working, so it's a natural fit.
        """

        controller_id = None
        t1 = time.time()
        while controller_id is None:
            node = self.redpanda.nodes[0]
            controller_id = r = requests.get(
                f"http://{node.account.hostname}:9644/v1/partitions/redpanda/controller/0",
            )
            if r.status_code != 200:
                time.sleep(1)
                continue

            controller_id = r.json()['leader_id']
            if controller_id == -1:
                time.sleep(1)
                controller_id = None
                continue

            if time.time() - t1 > 10:
                raise RuntimeError("Timed out waiting for a leader")

        leader_node = self.redpanda.get_node(controller_id)

        # Request to all nodes, with redirect-following disabled.  Expect success
        # from the leader, and redirect responses from followers.
        for i, node in enumerate(self.redpanda.nodes):
            # Redpanda config in ducktape has two listeners, one by IP and one by DNS (this simulates
            # nodes that have internal and external addresses).  The admin API redirects should
            # redirect us to the leader's IP for requests sent by IP, by DNS for requests sent by DNS.
            if alternate_listener:
                hostname = socket.gethostbyname(node.account.hostname)
                port = self.redpanda.ADMIN_ALTERNATE_PORT
                leader_name = socket.gethostbyname(
                    leader_node.account.hostname)

            else:
                hostname = node.account.hostname
                port = 9644
                leader_name = leader_node.account.hostname

            resp = requests.request(
                "post",
                f"http://{hostname}:{port}/v1/security/users",
                json={
                    'username': f'user_a_{i}',
                    'password': 'password',
                    'algorithm': "SCRAM-SHA-256"
                },
                allow_redirects=False)
            self.logger.info(
                f"Response: {resp.status_code} {resp.headers} {resp.text}")

            if node == leader_node:
                assert resp.status_code == 200
            else:
                # Check we were redirected to the proper listener of the leader node
                self.logger.info(
                    f"Response (redirect): {resp.status_code} {resp.headers.get('location', None)} {resp.text} {resp.history}"
                )
                assert resp.status_code == 307

                location = resp.headers.get('location', None)
                assert location is not None
                assert location.startswith(f"http://{leader_name}:{port}/")

                # Again, this time let requests follow the redirect
                resp = requests.request(
                    "post",
                    f"http://{hostname}:{port}/v1/security/users",
                    json={
                        'username': f'user_a_{i}',
                        'password': 'password',
                        'algorithm': "SCRAM-SHA-256"
                    },
                    allow_redirects=True)

                self.logger.info(
                    f"Response (follow redirect): {resp.status_code} {resp.text} {resp.history}"
                )
                assert resp.status_code == 200

    @cluster(num_nodes=3)
    def test_scram(self):
        topic = TopicSpec()

        client = self.make_superuser_client()
        client.create_topic(topic)

        try:
            # with incorrect password
            client = self.make_superuser_client("xxx")
            client.topics()
            assert False, "Listing topics should fail"
        except AssertionError as e:
            raise e
        except Exception as e:
            self.redpanda.logger.debug(e)
            pass

        # but it works with correct password
        client = self.make_superuser_client()
        topics = client.topics()
        print(topics)
        assert topic.name in topics

        # again!
        client = self.make_superuser_client()
        topics = client.topics()
        print(topics)
        assert topic.name in topics

        username = self.redpanda.SUPERUSER_CREDENTIALS.username
        self.delete_user(username)

        try:
            # now listing should fail because the user has been deleted. add
            # some delay to give user deletion time to propogate
            for _ in range(5):
                client = self.make_superuser_client()
                topics = client.topics()
                time.sleep(1)
            assert False, "Listing topics should fail"
        except AssertionError as e:
            raise e
        except Exception as e:
            self.redpanda.logger.debug(e)
            pass

        # recreate user
        algorithm = self.redpanda.SUPERUSER_CREDENTIALS.algorithm
        password = self.create_user(username, algorithm)

        # works ok again
        client = self.make_superuser_client(password_override=password)
        topics = client.topics()
        print(topics)
        assert topic.name in topics

        # update password
        new_password = self.update_user(username)

        try:
            # now listing should fail because the password is different
            for _ in range(5):
                client = self.make_superuser_client(password_override=password)
                client.topics()
                time.sleep(1)
            assert False, "Listing topics should fail"
        except AssertionError as e:
            raise e
        except Exception as e:
            self.redpanda.logger.debug(e)
            pass

        # but works ok with new password
        client = self.make_superuser_client(password_override=new_password)
        topics = client.topics()
        print(topics)
        assert topic.name in topics

        users = self.list_users()
        assert username in users


class SaslPlainTest(BaseScramTest):
    """
    These tests validate the functionality of the SASL/PLAIN
    authentication mechanism.
    """
    class ClientType(IntEnum):
        KCL = 1
        RPK = 2
        PYTHON_RDKAFKA = 3
        KCLI = 4

    class ScramType(IntEnum):
        SCRAM_SHA_256 = 1
        SCRAM_SHA_512 = 2

        def __str__(self):
            return self.name.replace("_", "-")

    def __init__(self, test_context):
        security = SecurityConfig()
        security.enable_sasl = True
        super(SaslPlainTest,
              self).__init__(test_context,
                             num_brokers=3,
                             security=security,
                             extra_node_conf={'developer_mode': True})

    def _enable_plain_authn(self):
        self.logger.debug("Enabling SASL PLAIN")
        admin = Admin(self.redpanda)
        admin.patch_cluster_config(
            upsert={'sasl_mechanisms': ['PLAIN', 'SCRAM']})

    def _make_client(
        self,
        client_type: ClientType,
        username_override: str | None = None,
        password_override: str | None = None,
        algorithm_override: str | None = None
    ) -> PythonLibrdkafka | RawKCL | RpkTool | KafkaCliTools:
        username, password, algorithm = self.redpanda.SUPERUSER_CREDENTIALS
        username = username_override or username
        password = password_override or password
        algorithm = algorithm_override or algorithm

        if client_type == self.ClientType.PYTHON_RDKAFKA:
            return PythonLibrdkafka(self.redpanda,
                                    username=username,
                                    password=password,
                                    algorithm=algorithm)
        elif client_type == self.ClientType.KCL:
            return RawKCL(self.redpanda,
                          username=username,
                          password=password,
                          sasl_mechanism=algorithm)
        elif client_type == self.ClientType.RPK:
            return RpkTool(self.redpanda,
                           username=username,
                           password=password,
                           sasl_mechanism=algorithm)
        elif client_type == self.ClientType.KCLI:
            return KafkaCliTools(self.redpanda,
                                 user=username,
                                 passwd=password,
                                 algorithm=algorithm)
        else:
            assert False, f'Unknown client type: {client_type}'

    def _make_topic(self, client: PythonLibrdkafka | RawKCL | RpkTool
                    | KafkaCliTools, expect_success: bool) -> TopicSpec:
        topic_name = "test-topic"
        topic = TopicSpec(name=topic_name)
        try:
            if isinstance(client, PythonLibrdkafka):
                client.create_topic(topic)
            elif isinstance(client, RawKCL):
                resp = client.create_topics(6, [{"name": topic_name}])
                self.logger.info(f"RESP: {resp}")
                if expect_success:
                    assert len(
                        resp
                    ) != 0, "Should have received response with SASL/PLAIN enabled"
                    assert resp[0][
                        'ErrorCode'] == 0, f"Expected error code 0, got {resp[0]['ErrorCode']}"
                    return
                else:
                    assert len(
                        resp
                    ) == 0, "Should not have received response with SASL/PLAIN disabled"
                    return
            elif isinstance(client, RpkTool):
                client.create_topic(topic=topic_name)
            elif isinstance(client, KafkaCliTools):
                client.create_topic(topic)
            else:
                assert False, f'Unknown client type: {client} ({type(client)})'
            assert expect_success, "Should have failed with SASL/PLAIN disabled"
        except RpkException as e:
            assert isinstance(
                client, RpkTool
            ), f"Should not have received an RPK exception from {client} ({type(client)})"
            assert not expect_success, f"Should not have failed with SASL/PLAIN enabled: {e}"
            assert "UNSUPPORTED_SASL_MECHANISM" in str(
                e), f"Expected UNSUPPORTED_SASL_MECHANISM, got {e}"
        except KafkaException as e:
            assert isinstance(
                client, PythonLibrdkafka
            ), f"Should not have received a KafkaException from {client} ({type(client)})"
            assert not expect_success, f"Should not have failed with SASL/PLAIN enabled: {e}"
            assert e.args[0].code(
            ) == KafkaError._TIMED_OUT, f"Expected KafkaError._TIMED_OUT, got {e.args[0].code()}"
        except KafkaCliToolsError as e:
            assert isinstance(
                client, KafkaCliTools
            ), f"Should not have received a KafkaCliToolsError from {client} ({type(client)})"
            assert not expect_success, f"Should not have failed with SASL/PLAIN enabled: {e}"
            assert "UnsupportedSaslMechanismException" in str(
                e
            ), f"Expected to see UnsupportedSaslMechanismException, got {e}"

    @cluster(num_nodes=3)
    @matrix(client_type=list(ClientType),
            scram_type=list(ScramType),
            sasl_plain_enabled=[True, False])
    def test_plain_authn(self, client_type, scram_type, sasl_plain_enabled):
        """
        This test validates that SASL/PLAIN works with common kafka client
        libraries:
        - Python librdkafka
        - Raw KCL
        - RPK
        - Kafka CLI tools

        This test will validate that SASL/PLAIN works with both SCRAM-SHA-256
        and SCRAM-SHA-512 users.
        """
        username = "test-user"
        password = "test-password"
        RpkTool(self.redpanda,
                username=self.redpanda.SUPERUSER_CREDENTIALS.username,
                password=self.redpanda.SUPERUSER_CREDENTIALS.password,
                sasl_mechanism=self.redpanda.SUPERUSER_CREDENTIALS.algorithm
                ).sasl_allow_principal(principal=username,
                                       operations=["all"],
                                       resource="topic",
                                       resource_name="*")
        self.create_user(username=username,
                         algorithm=str(scram_type),
                         password=password)

        if sasl_plain_enabled:
            self._enable_plain_authn()

        client = self._make_client(client_type,
                                   username_override=username,
                                   password_override=password,
                                   algorithm_override="PLAIN")
        self._make_topic(client, sasl_plain_enabled)


class SaslPlainTLSProvider(TLSProvider):
    def __init__(self, tls: TLSCertManager):
        self._tls = tls

    @property
    def ca(self) -> CertificateAuthority:
        return self._tls.ca

    def create_broker_cert(self, service: Service,
                           node: ClusterNode) -> Certificate:
        assert node in service.nodes
        return self._tls.create_cert(node.name)

    def create_service_client_cert(self, _: Service, name: str) -> Certificate:
        return self._tls.create_cert(socket.gethostname(), name=name)


class SaslPlainConfigTest(BaseScramTest):
    """
    These tests verify the behavior of Redpanda in different
    configurations with SASL/PLAIN enabled
    """

    LICENSE_CHECK_INTERVAL_SEC = 1

    def __init__(self, test_context):
        self.security = SecurityConfig()
        self.security.enable_sasl = True
        super(SaslPlainConfigTest, self).__init__(test_context,
                                                  num_brokers=3,
                                                  security=self.security)
        self.redpanda.set_environment({
            '__REDPANDA_PERIODIC_REMINDER_INTERVAL_SEC':
            f'{self.LICENSE_CHECK_INTERVAL_SEC}'
        })
        self.tls = TLSCertManager(self.logger)

    def setUp(self):
        pass

    def _start_cluster(self, enable_tls: bool):
        if enable_tls:
            self.security.tls_provider = SaslPlainTLSProvider(tls=self.tls)
            self.redpanda.set_security_settings(self.security)
        super().setUp()

    @cluster(num_nodes=3)
    def test_cannot_enable_plain_without_scram(self):
        """
        This test verifies that when enabling PLAIN you must also enable SCRAM
        """
        self._start_cluster(enable_tls=False)
        admin = Admin(self.redpanda)

        def validate_sasl_plain_mech(mechs: List[str]):
            try:
                admin.patch_cluster_config(upsert={'sasl_mechanisms': mechs})
                assert False, f"Should not have been able to set {mechs}"
            except HTTPError as e:
                assert e.response.status_code == 400, f"Expected 400, got {e.response.status_code}"
                response = json.loads(e.response.text)
                assert 'sasl_mechanisms' in response, f'Response missing "sasl_mechanisms": {response}'
                assert "SCRAM mechanism must be enabled if PLAIN is enabled" == response[
                    'sasl_mechanisms'], f"Invalid message in response: {response['sasl_mechansisms']}"

        validate_sasl_plain_mech(['PLAIN'])
        validate_sasl_plain_mech(['PLAIN', 'GSSAPI'])

    @cluster(num_nodes=3, log_allow_list=[re.compile('SASL/PLAIN is enabled')])
    @parametrize(enable_tls=True)
    @parametrize(enable_tls=False)
    def test_sasl_plain_log(self, enable_tls: bool):
        """
        This test verifies that a log message is emitted when SASL/PLAIN is enabled
        """
        self._start_cluster(enable_tls=enable_tls)
        wait_until_nag_is_set(
            redpanda=self.redpanda,
            check_interval_sec=self.LICENSE_CHECK_INTERVAL_SEC)
        admin = Admin(self.redpanda)
        admin.patch_cluster_config(
            upsert={'sasl_mechanisms': ['SCRAM', 'PLAIN']})

        self.logger.debug("Waiting for SASL/PLAIN message")

        def has_sasl_plain_log():
            # There is always at least one Kafka API with TLS disabled meaning
            # this will always log at the error level
            return self.redpanda.search_log_all(
                r"^ERROR.*SASL/PLAIN is enabled\. This is insecure and not recommended for production\.$"
            )

        wait_until(has_sasl_plain_log,
                   timeout_sec=self.LICENSE_CHECK_INTERVAL_SEC * 2,
                   err_msg="Failed to find SASL/PLAIN log message")


class ScramLiveUpdateTest(RedpandaTest):
    def __init__(self, test_context):
        super(ScramLiveUpdateTest, self).__init__(test_context, num_brokers=1)

    @cluster(num_nodes=1)
    def test_enable_sasl_live(self):
        """
        Verify that when enable_sasl is set to true at runtime, subsequent
        unauthenticated kafka clients are rejected.
        """

        unauthenticated_client = PythonLibrdkafka(self.redpanda)
        topic = TopicSpec(replication_factor=1)
        unauthenticated_client.create_topic(topic)
        assert len(unauthenticated_client.topics()) == 1

        # Switch on authentication
        admin = Admin(self.redpanda)
        admin.patch_cluster_config(upsert={'enable_sasl': True})

        # An unauthenticated client should be rejected
        try:
            unauthenticated_client.topics()
        except Exception as e:
            self.logger.exception(f"Unauthenticated: {e}")
        else:
            self.logger.error(
                "Unauthenticated client should have been rejected")
            assert False

        # Switch off authentication
        admin.patch_cluster_config(upsert={'enable_sasl': False})

        # An unauthenticated client should be accepted again
        assert len(unauthenticated_client.topics()) == 1


class ScramBootstrapUserTest(RedpandaTest):
    BOOTSTRAP_USERNAME = 'bob'
    BOOTSTRAP_PASSWORD = 'sekrit'

    # BOOTSTRAP_MECHANISM = 'SCRAM-SHA-512'

    def __init__(self, test_context, *args, **kwargs):
        # Configure the cluster as a user might configure it for secure
        # bootstrap: i.e. all auth turned on from moment of creation.

        self.mechanism = test_context.injected_args["mechanism"]
        self.expect_fail = test_context.injected_args.get("expect_fail", False)

        security_config = SecurityConfig()
        security_config.enable_sasl = True

        super().__init__(
            test_context,
            *args,
            environment={
                'RP_BOOTSTRAP_USER':
                f'{self.BOOTSTRAP_USERNAME}:{self.BOOTSTRAP_PASSWORD}:{self.mechanism}'
            },
            extra_rp_conf={
                'enable_sasl': True,
                'admin_api_require_auth': True,
                'superusers': ['bob']
            },
            security=security_config,
            superuser=SaslCredentials(self.BOOTSTRAP_USERNAME,
                                      self.BOOTSTRAP_PASSWORD, self.mechanism),
            **kwargs)

    def setUp(self):
        self.redpanda.start(expect_fail=self.expect_fail)
        if not self.expect_fail:
            self._create_initial_topics()

    def _check_http_status_everywhere(self, expect_status, callable):
        """
        Check that the callback results in an HTTP error with the
        given status code from all nodes in the cluster.  This enables
        checking that auth state has propagated as expected.

        :returns: true if all nodes throw an error with the expected status code
        """

        for n in self.redpanda.nodes:
            try:
                callable(n)
            except HTTPError as e:
                if e.response.status_code != expect_status:
                    return False
            else:
                return False

        return True

    @cluster(num_nodes=3)
    @parametrize(mechanism='SCRAM-SHA-512')
    @parametrize(mechanism='SCRAM-SHA-256')
    def test_bootstrap_user(self, mechanism):
        # Anonymous access should be refused
        admin = Admin(self.redpanda)
        with expect_http_error(403):
            admin.list_users()

        # Access using the bootstrap credentials should succeed
        admin = Admin(self.redpanda,
                      auth=(self.BOOTSTRAP_USERNAME, self.BOOTSTRAP_PASSWORD))
        assert self.BOOTSTRAP_USERNAME in admin.list_users()

        # Modify the bootstrap user's credential
        admin.update_user(self.BOOTSTRAP_USERNAME, "newpassword",
                          "SCRAM-SHA-256")

        # Getting 401 with old credentials everywhere will show that the
        # credential update has propagated to all nodes
        wait_until(lambda: self._check_http_status_everywhere(
            401, lambda n: admin.list_users(node=n)),
                   timeout_sec=10,
                   backoff_sec=0.5)

        # Using old password should fail
        with expect_http_error(401):
            admin.list_users()

        # Using new credential should succeed
        admin = Admin(self.redpanda,
                      auth=(self.BOOTSTRAP_USERNAME, 'newpassword'))
        admin.list_users()

        # Modified credential should survive a restart: this verifies that
        # the RP_BOOTSTRAP_USER setting does not fight with changes made
        # by other means.
        self.redpanda.restart_nodes(self.redpanda.nodes)
        admin.list_users()

    @cluster(num_nodes=1,
             log_allow_list=[
                 re.compile(r'std::invalid_argument.*Invalid SCRAM mechanism')
             ])
    @parametrize(mechanism='sCrAm-ShA-512', expect_fail=True)
    def test_invalid_scram_mechanism(self, mechanism, expect_fail):
        assert expect_fail
        assert self.redpanda.count_log_node(self.redpanda.nodes[0],
                                            "Invalid SCRAM mechanism")


class InvalidNewUserStrings(BaseScramTest):
    """
    Tests used to validate that strings with control characters are rejected
    when attempting to create users
    """
    def __init__(self, test_context):
        security = SecurityConfig()
        security.enable_sasl = False
        super(InvalidNewUserStrings,
              self).__init__(test_context,
                             num_brokers=3,
                             security=security,
                             extra_node_conf={'developer_mode': True})

    @cluster(num_nodes=3)
    def test_invalid_user_name(self):
        """
        Validates that usernames that contain control characters and usernames which
        do not match the SCRAM regex are properly rejected
        """
        username = generate_string_with_control_character(15)

        self.create_user(
            username=username,
            algorithm='SCRAM-SHA-256',
            expected_status_code=400,
            err_msg=
            f'Parameter contained invalid control characters: {username.translate(CONTROL_CHARS_MAP)}'
        )

        # Two ordinals (corresponding to ',' and '=') are explicitly excluded from SASL usernames
        for ordinal in [0x2c, 0x3d]:
            username = f"john{chr(ordinal)}doe"
            self.create_user(
                username=username,
                algorithm='SCRAM-SHA-256',
                expected_status_code=400,
                err_msg=f'Invalid SCRAM username {"{" + username + "}"}')

    @cluster(num_nodes=3)
    def test_invalid_alg(self):
        """
        Validates that algorithms that contain control characters are properly rejected
        """
        algorithm = generate_string_with_control_character(10)

        self.create_user(
            username="test",
            algorithm=algorithm,
            expected_status_code=400,
            err_msg=
            f'Parameter contained invalid control characters: {algorithm.translate(CONTROL_CHARS_MAP)}'
        )

    @cluster(num_nodes=3)
    def test_invalid_password(self):
        """
        Validates that passwords that contain control characters are properly rejected
        """
        password = generate_string_with_control_character(15)
        self.create_user(
            username="test",
            algorithm="SCRAM-SHA-256",
            password=password,
            expected_status_code=400,
            err_msg='Parameter contained invalid control characters: PASSWORD')


class EscapedNewUserStrings(BaseScramTest):

    # All of the non-control characters that need escaping
    NEED_ESCAPE = [
        '!',
        '"',
        '#',
        '$',
        '%',
        '&',
        '\'',
        '(',
        ')',
        '+',
        # ',', Excluded by SASLNAME regex
        '/',
        ':',
        ';',
        '<',
        # '=', Excluded by SASLNAME regex
        '>',
        '?',
        '[',
        '\\',
        ']',
        '^',
        '`',
        '{',
        '}',
        '~',
    ]

    @cluster(num_nodes=3)
    def test_update_delete_user(self):
        """
        Verifies that users whose names contain characters which require URL escaping can be subsequently deleted.
        i.e. that the username included with a delete request is properly unescaped by the admin server.
        """

        su_username = self.redpanda.SUPERUSER_CREDENTIALS.username

        users = []

        self.logger.debug(
            "Create some users with names that will require URL escaping")

        for ch in self.NEED_ESCAPE:
            username = f"john{ch}doe"
            self.create_user(username=username,
                             algorithm="SCRAM-SHA-256",
                             password="passwd",
                             expected_status_code=200)
            users.append(username)

        admin = Admin(self.redpanda)

        def _users_match(expected: list[str]):
            live_users = admin.list_users()
            live_users.remove(su_username)
            return len(expected) == len(live_users) and set(expected) == set(
                live_users)

        wait_until(lambda: _users_match(users), timeout_sec=5, backoff_sec=0.5)

        self.logger.debug(
            "We should be able to update and delete these users without issue")
        for username in users:
            self.update_user(username=username)
            self.delete_user(username=username)

        try:
            wait_until(lambda: _users_match([]),
                       timeout_sec=5,
                       backoff_sec=0.5)
        except TimeoutError:
            live_users = admin.list_users()
            live_users.remove(su_username)
            assert len(
                live_users
            ) == 0, f"Expected no users, got {len(live_users)}: {live_users}"


class SCRAMReauthTest(BaseScramTest):
    EXAMPLE_TOPIC = 'foo'

    MAX_REAUTH_MS = 2000
    PRODUCE_DURATION_S = MAX_REAUTH_MS * 2 / 1000
    PRODUCE_INTERVAL_S = 0.1
    PRODUCE_ITER = int(PRODUCE_DURATION_S / PRODUCE_INTERVAL_S)

    def __init__(self, test_context, **kwargs):
        security = SecurityConfig()
        security.enable_sasl = True
        super().__init__(
            test_context=test_context,
            num_brokers=3,
            security=security,
            extra_rp_conf={'kafka_sasl_max_reauth_ms': self.MAX_REAUTH_MS},
            **kwargs)

        username, password, algorithm = self.redpanda.SUPERUSER_CREDENTIALS
        self.rpk = RpkTool(self.redpanda,
                           username=username,
                           password=password,
                           sasl_mechanism=algorithm)

    @cluster(num_nodes=3)
    def test_scram_reauth(self):
        self.rpk.create_topic(self.EXAMPLE_TOPIC)
        su_client = self.make_superuser_client()
        producer = su_client.get_producer()
        producer.poll(1.0)

        expected_topics = set([self.EXAMPLE_TOPIC])
        wait_until(lambda: set(producer.list_topics(timeout=5).topics.keys())
                   == expected_topics,
                   timeout_sec=5)

        for i in range(0, self.PRODUCE_ITER):
            producer.poll(0.0)
            producer.produce(topic=self.EXAMPLE_TOPIC, key='bar', value=str(i))
            time.sleep(self.PRODUCE_INTERVAL_S)

        producer.flush(timeout=2)

        metrics = get_sasl_metrics(self.redpanda)
        self.redpanda.logger.debug(f"SASL metrics: {metrics}")
        assert (EXPIRATION_METRIC in metrics.keys())
        assert (metrics[EXPIRATION_METRIC] == 0
                ), "Client should reauth before session expiry"
        assert (REAUTH_METRIC in metrics.keys())
        assert (metrics[REAUTH_METRIC]
                > 0), "Expected client reauth on some broker..."
