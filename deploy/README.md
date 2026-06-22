# wr deploy

The static release binary is shipped to the target server by the ansible setup
and run under a systemd system unit.

## Layout

- The ansible configuration is held in ansible/ansible.cfg.
- The target hosts are named in ansible/inventory/hosts.yml under the webring group.
- The binary is shipped and the service is started by ansible/playbooks/wr.yml.
- The non-secret variables are held in ansible/playbooks/group_vars/all/main.yml.
- The systemd unit is ansible/files/wr.service, and the environment template is ansible/files/wr.env.j2.
- The secret template is ansible/vault.example.yml.

## Secrets

The GitHub OAuth client id and secret, the Telegram bot token, and the session
key are secrets. The real values are held in an encrypted vault beside the
variables.

```
cp ansible/vault.example.yml ansible/playbooks/group_vars/all/vault.yml
ansible-vault encrypt ansible/playbooks/group_vars/all/vault.yml
```

The real vault.yml is gitignored, so a plaintext secret is never committed.

## Deploy

The static release binary is built locally through `make MODE=rel`, and ../wr is
then copied to /usr/local/bin/wr on the server, the environment file is rendered,
the unit is installed, and the service is started. The service is run as the
dedicated wr system user with `Restart=always`. The whole flow is driven by the
deploy target from the repository root.

```
make -C src deploy ANSIBLE_ARGS=--ask-vault-pass
```

The playbook is run directly when the binary is already built.

```
cd ansible
ansible-playbook playbooks/wr.yml --ask-vault-pass
```

The target host and the target user are set per host in
ansible/inventory/hosts.yml.
