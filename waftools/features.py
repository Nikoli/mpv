from waflib.Options import OptionsContext

class Feature(object):
    def __init__(self, group, feature):
        self.group     = group
        self.identifier, self.attributes = feature

    def add_options(self):
        [self.add_option(option_rule) for option_rule in self.option_rules()]

    # private
    def add_option(self, rule):
        self.group.add_option(self.option(rule['state']),
                              action=rule['action'],
                              default=rule['default'],
                              dest=self.storage(),
                              help=self.help(rule['state']))

    def option_rules(self):
        return {
            'autodetect': [
                {'state': 'disable', 'action': 'store_false', 'default': True},
                {'state': 'enable',  'action': 'store_true',  'default': True},
            ],
            'disable': [
                {'state': 'enable',  'action': 'store_true',  'default': False},
            ],
            # enabled is not present since it doesn;t make any sense with
            # autodetect
        }[self.behaviour()]

    def behaviour(self):
        if 'default' in self.attributes:
            return self.attributes['default']
        else:
            return 'autodetect'


    def option(self, state):
        return "--{0}-{1}".format(state, self.identifier)

    def help(self, state):
        return "{0} {1} [{2}]" \
            .format(state, self.attributes['desc'], self.behaviour())

    def storage(self):
        return "enable_{0}".format(self.identifier)

def add_feature(group, feature):
    Feature(group, feature).add_options()

def parse_features(opt, group, features):
    group = opt.add_option_group(group)
    [add_feature(group, feature) for feature in reversed(features.items())]

OptionsContext.parse_features = parse_features
