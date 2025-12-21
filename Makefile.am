SUBDIRS = src tests

vagrant-clean:
	-for i in vagrant/*/Vagrantfile; do cd `dirname $$i` && (vagrant destroy -f || true) ; cd $$OLDPWD; done
	-rm -Rf vagrant/*/.vagrant vagrant/*/*.log

vagrant-test:
	-vagrant/test.rb

EXTRA_DIST = packaging/make_pkg.sh packaging/make_deb.sh packaging/make_bottle.sh Formula/cachefs.rb

.PHONY: pkg deb brew tap

pkg: all
	./packaging/make_pkg.sh "$(PACKAGE_VERSION)"

deb: all
	./packaging/make_deb.sh "$(PACKAGE_VERSION)"

brew tap: all
	./packaging/make_bottle.sh "$(PACKAGE_VERSION)"
