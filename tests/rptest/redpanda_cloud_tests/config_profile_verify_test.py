# Copyright 2021 Redpanda Data, Inc.
#
# Use of this software is governed by the Business Source License
# included in the file licenses/BSL.md
#
# As of the Change Date specified in that file, in accordance with
# the Business Source License, use of this software will be governed
# by the Apache License, Version 2.0

#from ducktape.tests.test import Test

#from rptest.services.redpanda import make_redpanda_service

from ducktape.tests.test import Test

from rptest.services.cluster import cluster
from rptest.services.redpanda import make_redpanda_service
from rptest.clients.installpack import InstallPackClient

import subprocess
import json

from rptest.tests.redpanda_cloud_test import RedpandaCloudTest


class ConfigProfileVerifyTest(RedpandaCloudTest):
    """
    Verify cluster infra/config match config profile used to launch - only applies to cloudv2
    """
    def __init__(self, test_context):
        super().__init__(test_context=test_context)
        self._ctx = test_context
        self._ipClient = InstallPackClient(
            self.redpanda._cloud_cluster.config.install_pack_url_template,
            self.redpanda._cloud_cluster.config.install_pack_auth_type,
            self.redpanda._cloud_cluster.config.install_pack_auth)

    def setUp(self):
        super().setUp()
        cloud_cluster = self.redpanda._cloud_cluster
        install_pack_version = cloud_cluster.get_install_pack_version()
        self._ip = self._ipClient.getInstallPack(install_pack_version)
        self._clusterId = cloud_cluster.cluster_id
        self._configProfile = self._ip['config_profiles'][
            cloud_cluster.config.config_profile_name]

    def test_config_profile_verify(self):
        self.logger.debug("Here we go")

        # assert isinstance(self.redpanda, RedpandaServiceCloud)
        match self._configProfile['cloud_provider']:
            case 'aws':
                self._check_aws_nodes()
            case 'gcp':
                self._check_gcp_nodes()
            case 'azure':
                self._check_azure_nodes()

        self._check_rp_config()

    def _check_rp_config(self):
        confRes = self.redpanda.kubectl.exec(
            'rpk redpanda admin config print --host 0')
        clusterConfig = json.loads(confRes)
        assert clusterConfig['cluster_id'] in (
            self._clusterId, f'rp-{self._clusterId}'
        ), f'asserting we got the config for the right cluster: expected {self._clusterId} to contain {clusterConfig["cluster_id"]}'

        for k, expected_v in self._configProfile["cluster_config"].items():
            actual_v = clusterConfig[k]
            self.logger.debug(
                "asserting cluster config key {} has expected value: {}  actual: {}"
                .format(k, expected_v, actual_v))
            if expected_v == "null":
                expected_v = None
            if actual_v != expected_v and "{}".format(actual_v) != expected_v:
                assert False, f"incorrect config value for key '{k}': {actual_v} != {expected_v}"

    def _check_aws_nodes(self):
        cmd = self.redpanda.kubectl._ssh_prefix() + [
            'aws', 'ec2', 'describe-instances',
            '--filters="Name=tag:Name, Values=redpanda-{}-rp"'.format(
                self._clusterId),
            '--query="Reservations[0].Instances[*].InstanceType"'
        ]
        res = subprocess.check_output(cmd)
        try:
            resd = json.loads(res)
        except json.JSONDecodeError as e:
            self.logger.error(
                f"Failed to parse AWS describe-instances response: {res}")
            raise ValueError(
                f"Failed to parse AWS describe-instances response: {e}")

        self.logger.debug(
            "asserting nodes_count: expected: {}, actual: {}".format(
                self._configProfile['nodes_count'], len(resd)))
        assert len(resd) == self._configProfile['nodes_count']

        self.logger.debug(
            "asserting machineType: expected: {}, actual: {}".format(
                self._configProfile['machine_type'], resd[0]))
        assert resd[0] == self._configProfile['machine_type']

    def _check_azure_nodes(self):
        jsonpath = '{..labels.node\\.kubernetes\\.io/instance-type}'
        cmd = self.redpanda.kubectl._ssh_prefix() + [
            'kubectl', 'get', 'nodes',
            '--selector', 'redpanda-node=true',
            '--output', f'jsonpath-as-json="{jsonpath}"'
        ] # yapf: disable

        output = subprocess.check_output(cmd)
        self.logger.debug(f'nodes: {output}')
        nodes = json.loads(output)
        try:
            nodes = json.loads(nodes)
        except json.JSONDecodeError as e:
            self.logger.error(
                f"Failed to parse kubectl get nodes response: {nodes}")
            raise ValueError(
                f"Failed to parse kubectl get nodes response: {e}")

        config_nodes_count = self._configProfile['nodes_count']
        actual_nodes_count = len(nodes)
        assert actual_nodes_count == config_nodes_count, f"expected nodes_count: {config_nodes_count}, actual: {actual_nodes_count}"

        config_machine_type = self._configProfile['machine_type']
        actual_machine_type = nodes[0]
        assert actual_machine_type == config_machine_type, f"expected machineType: {config_machine_type}, actual: {actual_machine_type}"

    def _check_gcp_nodes(self):
        cmd = self.redpanda.kubectl._ssh_prefix() + [
            'gcloud', 'compute', 'instances', 'list', '--filter',
            '"tags.items=redpanda-node tags.items=redpanda-{}"'.format(
                self._clusterId), '--format="json(name,machineType,disks)"'
        ]
        res = subprocess.check_output(cmd)
        try:
            resd = json.loads(res)
        except json.JSONDecodeError as e:
            self.logger.error(
                f"Failed to parse gcloud compute instances response: {res}")
            raise ValueError(
                f"Failed to parse gcloud compute instances response: {e}")

        self.logger.debug(
            "asserting machineType: expected: {}, actual: {}".format(
                self._configProfile['machine_type'], resd[0]['machineType']))
        assert resd[0]['machineType'].endswith(
            self._configProfile['machine_type'])

        self.logger.debug(
            "asserting nodes_count: expected: {}, actual: {}".format(
                self._configProfile['nodes_count'], len(resd)))
        assert len(resd) == self._configProfile['nodes_count']
        # check disks (we only do this for gcp as they are dynamically added to node)
        for n in resd:
            disks = [disk for disk in n['disks'] if disk['type'] == "SCRATCH"]
            total = 0
            for d in disks:
                total += int(d['diskSizeGb']) * 1024 * 1024 * 1024
            self.logger.debug(
                "asserting storage for redpanda node {}: expected: {}, actual: {}"
                .format(n["name"], self._configProfile["storage_size_bytes"],
                        total))
            assert total == self._configProfile['storage_size_bytes']
